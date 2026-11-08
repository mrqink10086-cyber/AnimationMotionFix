#include "MotionMath.h"
#include "PendingPlanes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using Plane = std::array<float, 4>;
	using Cache = AMF::PendingPlanes<std::uintptr_t, Plane>;

	void Require(bool a_value, const char* a_message)
	{
		if (!a_value) {
			std::cerr << a_message << '\n';
			std::abort();
		}
	}

	class CountingResource : public std::pmr::memory_resource
	{
	public:
		std::size_t allocations{ 0 };
		bool fail{ false };

	private:
		void* do_allocate(std::size_t a_bytes, std::size_t a_alignment) override
		{
			if (fail) {
				throw std::bad_alloc();
			}
			++allocations;
			return std::pmr::new_delete_resource()->allocate(a_bytes, a_alignment);
		}
		void do_deallocate(void* a_pointer, std::size_t a_bytes, std::size_t a_alignment) override
		{
			std::pmr::new_delete_resource()->deallocate(a_pointer, a_bytes, a_alignment);
		}
		bool do_is_equal(const std::pmr::memory_resource& a_other) const noexcept override
		{
			return this == &a_other;
		}
	};

	void TestPitch()
	{
		const auto halfPi = std::numbers::pi_v<float> / 2.0f;
		for (const auto pitch : { halfPi, -halfPi, halfPi + 0.01f, -halfPi - 0.01f,
			std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
			halfPi - 0.00001f, -halfPi + 0.00001f }) {
			float y = 3.0f;
			float z = 4.0f;
			Require(!AMF::UndoPitch(pitch, y, z) && y == 3.0f && z == 4.0f, "singular/invalid pitch must leave output unchanged");
		}
		float y = std::numeric_limits<float>::max();
		float z = 4.0f;
		Require(!AMF::UndoPitch(1.0f, y, z) && y == std::numeric_limits<float>::max() && z == 4.0f, "overflow must leave output unchanged");
		y = 0.0f;
		z = std::numeric_limits<float>::infinity();
		Require(!AMF::UndoPitch(0.0f, y, z), "nonfinite translation must be rejected");
		y = -0.0f;
		z = 4.0f;
		Require(AMF::UndoPitch(0.0f, y, z) && std::signbit(y) && z == 4.0f, "zero pitch must preserve signed zero");

		std::mt19937 random(11701597);
		std::uniform_real_distribution<float> angles(-1.55f, 1.55f);
		std::uniform_real_distribution<float> distance(-100.0f, 100.0f);
		for (int i = 0; i < 100000; ++i) {
			const auto pitch = angles(random);
			const auto originalY = distance(random);
			const auto originalZ = distance(random);
			y = originalY * std::cos(pitch);
			z = originalZ + originalY * std::sin(pitch);
			Require(AMF::UndoPitch(pitch, y, z), "regular pitch must be accepted");
			Require(std::abs(y - originalY) < 0.001f && std::abs(z - originalZ) < 0.001f, "pitch inversion must recover unpitched translation");
		}
		for (const auto pitch : { halfPi - 0.0002f, -halfPi + 0.0002f }) {
			y = 1.0f;
			z = 2.0f;
			Require(AMF::UndoPitch(pitch, y, z) && std::isfinite(y) && std::isfinite(z), "valid pitch outside exclusion band must work");
		}
	}

	void TestPlanes()
	{
		CountingResource resource;
		Cache cache(&resource);
		const Plane first{ 1, 2, 3, 4 };
		const Plane second{ 5, 6, 7, 8 };
		Require(!cache.Take(1), "missing controller");
		Require(cache.Remember(1, first) && cache.Remember(1, second), "remember duplicate controller");
		Require(cache.Take(1) == first && !cache.Take(1), "first plane wins and is consumed once");
		Require(cache.Remember(1, first), "remember before delete");
		cache.Erase(1);
		Require(cache.Remember(1, second) && cache.Take(1) == second, "deletion/address reuse must not leak an old plane");
		Require(cache.Remember(2, first), "remember before game clear");
		cache.Clear();
		Require(!cache.Take(2), "game clear removes pending planes");
		for (std::uintptr_t i = 0; i < 512; ++i) {
			Require(cache.Remember(i, first), "warm pool");
		}
		cache.Clear();
		const auto warmAllocations = resource.allocations;
		for (int pass = 0; pass < 100; ++pass) {
			for (std::uintptr_t i = 0; i < 512; ++i) {
				Require(cache.Remember(i, first), "reuse pool");
			}
			for (std::uintptr_t i = 0; i < 512; ++i) {
				Require(cache.Take(i) == first, "reuse pool result");
			}
		}
		Require(resource.allocations == warmAllocations, "warm pool must reuse upstream allocations");

		CountingResource failingResource;
		Cache failingCache(&failingResource);
		failingResource.fail = true;
		std::uintptr_t failedKey = 1;
		while (failedKey < 10000 && failingCache.Remember(failedKey, first)) {
			++failedKey;
		}
		Require(failedKey < 10000 && !failingCache.Take(failedKey), "allocation failure must return false without partial insertion");
		failingResource.fail = false;
		Require(failingCache.Remember(failedKey, first) && failingCache.Take(failedKey) == first, "cache recovers after allocation failure");
	}

	void TestThreads()
	{
		Cache cache;
		std::vector<std::thread> threads;
		for (std::uintptr_t worker = 0; worker < 8; ++worker) {
			threads.emplace_back([&, worker] {
				const auto key = worker + 1;
				const Plane first{ static_cast<float>(worker), 2, 3, 4 };
				const Plane second{ -1, -2, -3, -4 };
				for (int i = 0; i < 20000; ++i) {
					Require(cache.Remember(key, first) && cache.Remember(key, second), "concurrent insert");
					Require(cache.Take(key) == first, "concurrent first-wins/consume");
					Require(cache.Remember(key, second), "concurrent reuse");
					cache.Erase(key);
					Require(!cache.Take(key), "concurrent deletion");
				}
			});
		}
		for (auto& worker : threads) {
			worker.join();
		}
		std::atomic<bool> start{ false };
		threads.clear();
		for (int worker = 0; worker < 4; ++worker) {
			threads.emplace_back([&] {
				while (!start.load(std::memory_order_acquire)) {
					std::this_thread::yield();
				}
				for (int i = 0; i < 10000; ++i) {
					Require(cache.Remember(1, { 1, 2, 3, 4 }), "shared-key insert");
					const auto plane = cache.Take(1);
					Require(!plane || *plane == Plane{ 1, 2, 3, 4 }, "shared-key consume under clear");
					cache.Clear();
				}
			});
		}
		start.store(true, std::memory_order_release);
		for (auto& worker : threads) {
			worker.join();
		}
	}

	class BaselinePlanes
	{
	public:
		bool Remember(std::uintptr_t a_key, const Plane& a_plane)
		{
			std::unique_lock lock(mutex_);
			planes_.emplace(a_key, a_plane);
			return true;
		}
		std::optional<Plane> Take(std::uintptr_t a_key)
		{
			std::unique_lock lock(mutex_);
			const auto entry = planes_.find(a_key);
			if (entry == planes_.end()) {
				return std::nullopt;
			}
			auto result = entry->second;
			planes_.erase(entry);
			return result;
		}
	private:
		std::shared_mutex mutex_;
		std::unordered_map<std::uintptr_t, Plane> planes_;
	};

	template <class T>
	double Benchmark(std::uintptr_t a_batch)
	{
		T cache;
		std::size_t checksum = 0;
		const auto run = [&](std::size_t rounds) {
			for (std::size_t pass = 0; pass < rounds; ++pass) {
				for (std::uintptr_t i = 0; i < a_batch; ++i) {
					Require(cache.Remember(i, { 1, 2, 3, 4 }), "benchmark insert");
				}
				for (std::uintptr_t i = 0; i < a_batch; ++i) {
					const auto plane = cache.Take(i);
					Require(plane.has_value(), "benchmark consume");
					checksum += static_cast<std::size_t>((*plane)[0]);
				}
			}
		};
		run(100);
		const auto rounds = 500000 / a_batch;
		const auto start = std::chrono::steady_clock::now();
		run(rounds);
		const auto elapsed = std::chrono::steady_clock::now() - start;
		Require(checksum == (rounds + 100) * a_batch, "benchmark checksum");
		return std::chrono::duration<double, std::nano>(elapsed).count() / static_cast<double>(rounds * a_batch);
	}
}

int main(int a_argc, char** a_argv)
{
	TestPitch();
	TestPlanes();
	TestThreads();
	std::cout << "PASS: pitch recovery/boundaries, plane lifetime/allocation failure/reuse, 8-thread churn and concurrent clear\n";
	if (a_argc > 1 && std::string_view(a_argv[1]) == "--benchmark") {
		for (const std::uintptr_t batch : { 1, 16, 64, 512 }) {
			const auto baseline = Benchmark<BaselinePlanes>(batch);
			const auto pooled = Benchmark<Cache>(batch);
			std::cout << "batch=" << batch << " baseline_ns/pair=" << baseline << " pooled_ns/pair=" << pooled << " ratio=" << baseline / pooled << '\n';
		}
	}
}
