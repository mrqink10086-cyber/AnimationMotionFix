#pragma once

#include <atomic>

namespace AMF
{
	class AMFSettings : public REX::Singleton<AMFSettings>
	{
	public:
		enum MovMagMode : long
		{
			kGlobalEnable = 0,
			kDynamic,
			kGlobalDisable
		};

		void LoadSettings();

		std::atomic<bool> enablePitchTranslationFix{ true };
		std::atomic<bool> disablePlayerRotationMagnetism{ true };

		std::atomic<MovMagMode> playerMovementMagnetismMode{ kDynamic };
		std::atomic<MovMagMode> npcMovementMagnetismMode{ kDynamic };

		static bool HasAnimMotionData(RE::Actor* a_actor);
	};
	using MovMagMode = AMFSettings::MovMagMode;

}
