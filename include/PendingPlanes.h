#pragma once

#include <memory_resource>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace AMF
{
	template <class Controller, class Plane>
	class PendingPlanes
	{
	public:
		explicit PendingPlanes(std::pmr::memory_resource* a_upstream = std::pmr::get_default_resource()) :
			pool_(a_upstream)
		{}

		bool Remember(Controller a_controller, const Plane& a_plane)
		{
			try {
				std::lock_guard lock(mutex_);
				planes_.try_emplace(a_controller, a_plane);
				return true;
			} catch (const std::bad_alloc&) {
				return false;
			}
		}

		std::optional<Plane> Take(Controller a_controller)
		{
			std::lock_guard lock(mutex_);
			const auto entry = planes_.find(a_controller);
			if (entry == planes_.end()) {
				return std::nullopt;
			}
			auto plane = entry->second;
			planes_.erase(entry);
			return plane;
		}

		void Erase(Controller a_controller)
		{
			std::lock_guard lock(mutex_);
			planes_.erase(a_controller);
		}

		void Clear()
		{
			std::lock_guard lock(mutex_);
			planes_.clear();
		}

	private:
		std::mutex mutex_;
		std::pmr::unsynchronized_pool_resource pool_;
		std::pmr::unordered_map<Controller, Plane> planes_{ &pool_ };
	};
}
