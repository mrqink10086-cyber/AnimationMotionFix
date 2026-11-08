#pragma once

#include "HookSites.h"
#include "PendingPlanes.h"

namespace AMF
{
	// Fix the game engine's translation reduction bug when npcs has pitch angle offset
	class FixPitchTransHandler
	{
		static bool RevertPitchRotation(RE::Actor* a_actor, RE::NiPoint3& a_translation);

		struct Hook : Xbyak::CodeGenerator
		{
			Hook(std::uintptr_t func, std::uintptr_t ret)
			{
				Xbyak::Label hookLabel;
				Xbyak::Label retnLabel;

				mov(r8, rdi);  // rdi = Actor*, same on AE, VR
				call(ptr[rip + hookLabel]);

				jmp(ptr[rip + retnLabel]);

				L(hookLabel);
				dq(func);

				L(retnLabel);
				dq(ret);
			}
		};

	public:
		static void InstallHook()
		{
			const auto target = HookSites::PitchTranslation.Address();
			auto call = Hook(stl::unrestricted_cast<std::uintptr_t>(Hook_ConvertMoveDirToTranslation), target + 5);
			call.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* code = trampoline.allocate(call.getSize());
			std::memcpy(code, call.getCode(), call.getSize());
			ConvertMoveDirToTranslation = trampoline.write_branch<5>(target, code);

			SKSE::log::info("{} Done!", __FUNCTION__);
		}

	private:
		static void Hook_ConvertMoveDirToTranslation(const RE::NiPoint3& a_angle, RE::NiPoint3& a_outDirection, RE::Actor* a_actor);
		static inline REL::Relocation<void (*)(const RE::NiPoint3& a_angle, RE::NiPoint3& a_outDirection)> ConvertMoveDirToTranslation;

		FixPitchTransHandler() = delete;
		~FixPitchTransHandler() = delete;
	};

	class AttackMagnetismHandler
	{
	public:
		static bool ShouldDisableMovementMagnetism(RE::Actor* a_actor);

		//Disable player 's rotation magnetism effect in combat.
		class PlayerRotateMagnetismHook
		{
		public:
			static void InstallHook()
			{
				auto& trampoline = SKSE::GetTrampoline();

				func = trampoline.write_call<5>(HookSites::PlayerRotation.Address(), UpdateMagnetism);

				SKSE::log::info("{} Done!", __FUNCTION__);
			}

		private:
			static void UpdateMagnetism(RE::PlayerCharacter* a_player, float a_delta, RE::NiPoint3& a_translation, float& a_rotationZ);
			static inline REL::Relocation<decltype(UpdateMagnetism)> func;
		};

		//Disable actor's movement magnetism effect when performing animation driven attacking.
		class MovementMagnetismHook
		{
		public:
			static void InstallHook()
			{
				auto& trampoline = SKSE::GetTrampoline();
				func = trampoline.write_call<5>(HookSites::Movement.Address(), Hook_IsStartingMeleeAttack);

				SKSE::log::info("{} Done!", __FUNCTION__);
			}

		private:
			static bool Hook_IsStartingMeleeAttack(RE::Actor* a_actor);
			static inline REL::Relocation<decltype(Hook_IsStartingMeleeAttack)> func;
		};

	private:
		AttackMagnetismHandler() = delete;
		~AttackMagnetismHandler() = delete;
	};

	//After disable movement magnetism, npcs would pushing away their attack target during animation driven attacking, so we need this handler to prevent the pushing.
	class PushCharacterHandler
	{
	public:
		static bool ShouldPreventAttackPushing(RE::Actor* a_pusher, RE::Actor* a_target);
		static bool ShouldPreventAttackPushing(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target);

		static RE::TESObjectREFRPtr GetActor(RE::bhkCharacterController* a_charCtrl);
		static void ClearPendingPlanes();

		// Prevent a proxy character controller pushing another proxy controller when performing animation dirven attacking.
		class ProxyPushProxyHandler
		{
		public:
			static void InstallHook()
			{
				auto& trampoline = SKSE::GetTrampoline();

				func = trampoline.write_call<5>(HookSites::ProxyProxy.Address(), Hook_PushTargetCharacter);

				SKSE::log::info("{} Done!", __FUNCTION__);
			}

		private:
			static void Hook_PushTargetCharacter(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target, RE::hkContactPoint* a_contactPoint);
			static inline REL::Relocation<decltype(Hook_PushTargetCharacter)> func;
		};

		// Prevent a proxy character controller pushing another rigid body controller when performing animation dirven attacking.
		class ProxyPushRigidBodyHandler
		{
		public:
			static void InstallHook()
			{
				auto& trampoline = SKSE::GetTrampoline();

				func = trampoline.write_call<5>(HookSites::ProxyRigid.Address(), Hook_PushTargetCharacter);

				SKSE::log::info("{} Done!", __FUNCTION__);
			}

		private:
			static void Hook_PushTargetCharacter(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target, RE::hkContactPoint* a_contactPoint);
			static inline REL::Relocation<decltype(Hook_PushTargetCharacter)> func;
		};

		// Prevent a rigid body character controller pushing another proxy controller when performing animation dirven attacking.
		class RigidBodyPushProxyHandler
		{
			static inline PendingPlanes<RE::bhkCharRigidBodyController*, RE::hkVector4> charCtrlPlanes;

		public:
			static void ClearPendingPlanes() { charCtrlPlanes.Clear(); }

			static void InstallHook()
			{
				REL::Relocation<std::uintptr_t> Vtbl_ProxyCtrl{ RE::VTABLE_bhkCharProxyController[0] };
				ProcessConstraintsCallback = Vtbl_ProxyCtrl.write_vfunc(0x1, &Hook_ProcessConstraintsCallback);

				auto& trampoline = SKSE::GetTrampoline();
				UpdateForAnimationAttack = trampoline.write_call<5>(HookSites::RigidProxy.Address(), Hook_UpdateForAnimationAttack);

				REL::Relocation<std::uintptr_t> Vtbl_RigidBodyCtrl{ RE::VTABLE_bhkCharRigidBodyController[0] };
				DeleteThis = Vtbl_RigidBodyCtrl.write_vfunc(0x1, &Hook_DeleteThis);
				SKSE::log::info("{} Done!", __FUNCTION__);
			}

		private:
			static void Hook_ProcessConstraintsCallback(RE::bhkCharProxyController* a_proxyCtrl, const RE::hkpCharacterProxy* a_proxy, const RE::hkArray<RE::hkpRootCdPoint>& a_manifold, RE::hkpSimplexSolverInput& a_input);
			static inline REL::Relocation<decltype(&RE::bhkCharProxyController::ProcessConstraintsCallback)> ProcessConstraintsCallback;

			static void Hook_UpdateForAnimationAttack(RE::bhkCharacterController* a_charCtrl);
			static inline REL::Relocation<decltype(Hook_UpdateForAnimationAttack)> UpdateForAnimationAttack;

			static void Hook_DeleteThis(RE::bhkCharRigidBodyController* a_charCtrl);
			static inline REL::Relocation<decltype(&RE::bhkCharRigidBodyController::DeleteThis)> DeleteThis;
		};

		// Prevent a rigid body character controller pushing another rigid body controller when performing animation dirven attacking.
		class RigidBodyPushRigidBodyHandler
		{
		public:
			static void InstallHook()
			{
				auto& trampoline = SKSE::GetTrampoline();
				PushTargetCharacter = trampoline.write_call<5>(HookSites::RigidRigid.Address(), Hook_PushTargetCharacter);

				REL::Relocation<std::uintptr_t> Vtbl{ RE::VTABLE_FOCollisionListener[0] };
				ContactPointCallback = Vtbl.write_vfunc(0x0, &Hook_ContactPointCallback);

				SKSE::log::info("{} Done!", __FUNCTION__);
			}

		private:
			static void Hook_PushTargetCharacter(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target, RE::hkContactPoint* a_contactPoint);
			static inline REL::Relocation<decltype(Hook_PushTargetCharacter)> PushTargetCharacter;

			static void Hook_ContactPointCallback(RE::FOCollisionListener* a_listener, const RE::hkpContactPointEvent&);
			static inline REL::Relocation<decltype(&RE::FOCollisionListener::ContactPointCallback)> ContactPointCallback;
		};

	private:
		PushCharacterHandler() = delete;
		~PushCharacterHandler() = delete;
	};
}
