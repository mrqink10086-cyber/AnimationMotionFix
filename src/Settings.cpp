#include "Settings.h"
#include "AnimationMotionRevolutionAPI.h"
#include <SimpleIni.h>

namespace AMF
{
	static std::atomic<AMR_API::IVAMR1*> amr{ nullptr };

	void AMFSettings::LoadSettings()
	{
		const auto path = std::format("Data/SKSE/Plugins/{}.ini", Plugin::NAME);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(path.c_str());

		enablePitchTranslationFix = ini.GetBoolValue("Fix", "EnablePitchTranslationFix");

		constexpr const char* section = "Tweak";
		disablePlayerRotationMagnetism = ini.GetBoolValue(section, "DisablePlayerRotationMagnetism");
		playerMovementMagnetismMode = MovMagMode(ini.GetLongValue(section, "PlayerMovementMagnetismMode"));
		npcMovementMagnetismMode = MovMagMode(ini.GetLongValue(section, "NpcMovementMagnetismMode"));

		auto* api = static_cast<AMR_API::IVAMR1*>(AMR_API::RequestPluginAPI());
		amr.store(api, std::memory_order_release);
		if (!api) {
			SKSE::log::warn("Not AMR API Found!");
		}
	}

	bool AMFSettings::HasAnimMotionData(RE::Actor* a_actor)
	{
		auto* api = amr.load(std::memory_order_acquire);
		if (!api || !a_actor) {
			return false;
		}
		RE::BSAnimationGraphManagerPtr manager;
		if (!a_actor->GetAnimationGraphManager(manager) || !manager) {
			return false;
		}
		RE::BSTSmartPointer<RE::BShkbAnimationGraph> graph;
		{
			RE::BSSpinLockGuard lock(manager->GetRuntimeData().updateLock);
			if (!manager->graphs.empty()) {
				graph = manager->graphs[0];
			}
		}
		return graph && api->HasAnimMotionData(&graph->characterInstance);
	}
}
