#pragma once

namespace AMF::HookSites
{
	struct CallSite
	{
		std::string_view name;
		std::uint64_t seID;
		std::uint64_t aeID;
		std::ptrdiff_t seOffset;
		std::ptrdiff_t aeOffset;
		std::uintptr_t seTargetRVA;
		std::uintptr_t aeTargetRVA;

		[[nodiscard]] std::uintptr_t Address() const
		{
			return REL::Relocation<std::uintptr_t>{ REL::RelocationID(seID, aeID), REL::Relocate(seOffset, aeOffset) }.address();
		}
	};

	inline constexpr CallSite PitchTranslation{ "PitchTranslation", 36365, 37356, 0x365, 0x3FE, 0x10FA2F0, 0x11F19A0 };
	inline constexpr CallSite PlayerRotation{ "PlayerRotation", 39379, 40451, 0x42, 0x42, 0x6CF2A0, 0x7635E0 };
	inline constexpr CallSite Movement{ "Movement", 36357, 37348, 0x222, 0x1FB, 0x6274A0, 0x6B8EA0 };
	inline constexpr CallSite ProxyProxy{ "ProxyProxy", 77248, 79134, 0x3F, 0x37, 0xDC1C60, 0xEA13A0 };
	inline constexpr CallSite ProxyRigid{ "ProxyRigid", 77244, 79130, 0x3CD, 0x3B1, 0xDC1C60, 0xEA13A0 };
	inline constexpr CallSite RigidProxy{ "RigidProxy", 77321, 79201, 0x159, 0x147, 0xDF35B0, 0xED5360 };
	inline constexpr CallSite RigidRigid{ "RigidRigid", 77306, 79186, 0x29F, 0x2A9, 0xDC1C60, 0xEA13A0 };

	[[nodiscard]] bool Validate();
}
