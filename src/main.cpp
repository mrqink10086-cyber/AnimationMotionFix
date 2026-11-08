#include "Hooks.h"
#include "Settings.h"

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	if (a_skse->IsEditor()) {
		return false;
	}
	const auto runtime = a_skse->RuntimeVersion();
	const bool supportedRuntime = runtime == SKSE::RUNTIME_SSE_1_5_97 || runtime == SKSE::RUNTIME_SSE_1_6_1170;
	const auto minimumSKSE = runtime == SKSE::RUNTIME_SSE_1_5_97 ? REL::Version{ 2, 0, 20, 0 } : REL::Version{ 2, 2, 6, 0 };
	if (!supportedRuntime || a_skse->SKSEVersion() < minimumSKSE.pack()) {
		return false;
	}

	const auto logDirectory = SKSE::log::log_directory();
	if (!logDirectory) {
		return false;
	}
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*logDirectory / "AnimationMotionFix.log").string(), true);
	spdlog::set_default_logger(std::make_shared<spdlog::logger>("global log", std::move(sink)));
	spdlog::set_level(spdlog::level::info);
	spdlog::flush_on(spdlog::level::info);

	SKSE::Init(a_skse);
	SKSE::log::info("{} v{}; runtime {}; SKSE {}", Plugin::NAME, Plugin::VERSION.string(),
		runtime.string(), REL::Version::unpack(a_skse->SKSEVersion()).string());
	if (!AMF::HookSites::Validate()) {
		return false;
	}

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_message) {
			if (a_message && a_message->type == SKSE::MessagingInterface::kPostLoad) {
				AMF::AMFSettings::GetSingleton()->LoadSettings();
			} else if (a_message && (a_message->type == SKSE::MessagingInterface::kPreLoadGame ||
				a_message->type == SKSE::MessagingInterface::kNewGame)) {
				AMF::PushCharacterHandler::ClearPendingPlanes();
			}
		})) {
		SKSE::log::error("Failed to register the SKSE message listener.");
		return false;
	}

	SKSE::AllocTrampoline(256);
	AMF::FixPitchTransHandler::InstallHook();
	AMF::AttackMagnetismHandler::PlayerRotateMagnetismHook::InstallHook();
	AMF::AttackMagnetismHandler::MovementMagnetismHook::InstallHook();
	AMF::PushCharacterHandler::ProxyPushProxyHandler::InstallHook();
	AMF::PushCharacterHandler::ProxyPushRigidBodyHandler::InstallHook();
	AMF::PushCharacterHandler::RigidBodyPushProxyHandler::InstallHook();
	AMF::PushCharacterHandler::RigidBodyPushRigidBodyHandler::InstallHook();
	return true;
}
