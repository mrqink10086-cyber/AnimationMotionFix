#include "HookSites.h"

namespace AMF::HookSites
{
	bool Validate()
	{
		const auto base = REL::Module::get().base();
		for (const auto& site : { PitchTranslation, PlayerRotation, Movement, ProxyProxy, ProxyRigid, RigidProxy, RigidRigid }) {
			const auto address = site.Address();
			std::int32_t displacement;
			std::memcpy(&displacement, reinterpret_cast<const void*>(address + 1), sizeof(displacement));
			const auto target = address + 5 + displacement;
			const auto expected = base + REL::Relocate(site.seTargetRVA, site.aeTargetRVA);
			if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8 || target != expected) {
				SKSE::log::error("{} hook mismatch at RVA {:#x}; expected call target {:#x}, found {:#x}.",
					site.name, address - base, expected - base, target - base);
				return false;
			}
		}

		struct VirtualSite
		{
			std::string_view name;
			REL::VariantID id;
			std::size_t slot;
			std::uintptr_t seTargetRVA;
			std::uintptr_t aeTargetRVA;
		};
		const std::array sites{
			VirtualSite{ "ProcessConstraintsCallback", RE::VTABLE_bhkCharProxyController[0], 1, 0xDEF090, 0xED0B40 },
			VirtualSite{ "DeleteThis", RE::VTABLE_bhkCharRigidBodyController[0], 1, 0xC61A30, 0xD27520 },
			VirtualSite{ "ContactPointCallback", RE::VTABLE_FOCollisionListener[0], 0, 0x39C690, 0x3F4B90 }
		};
		for (const auto& site : sites) {
			const auto table = REL::Relocation<std::uintptr_t>{ site.id }.address();
			const auto target = reinterpret_cast<const std::uintptr_t*>(table)[site.slot];
			const auto expected = base + REL::Relocate(site.seTargetRVA, site.aeTargetRVA);
			if (target != expected) {
				SKSE::log::error("{} vtable mismatch at RVA {:#x} slot {}; expected {:#x}, found {:#x}.",
					site.name, table - base, site.slot, expected - base, target - base);
				return false;
			}
		}
		SKSE::log::info("Validated seven call sites and three virtual hooks.");
		return true;
	}
}
