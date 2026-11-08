#include "Hooks.h"
#include "Settings.h"
#include "MotionMath.h"

namespace AMF
{
	static bool IsValidActor(const RE::TESObjectREFRPtr& actor)
	{
		return actor && actor->IsActor() && actor->IsInitialized() && actor->Is3DLoaded();
	}

	bool FixPitchTransHandler::RevertPitchRotation(RE::Actor* a_actor, RE::NiPoint3& a_translation)
	{
		if (!a_actor || !AMFSettings::GetSingleton()->enablePitchTranslationFix.load(std::memory_order_relaxed) || a_actor->IsPlayerRef())
			return false;

		const auto actorState = a_actor->AsActorState();
		if (!actorState || actorState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal || actorState->IsFlying())
			return false;

		if (!a_actor->IsMovementAnimationDriven() || (!a_actor->IsAnimationDriven() && !a_actor->IsRotationAllowed())) {
			return false;
		}
		bool bIsSynced = false;
		if (a_actor->GetGraphVariableBool(RE::FixedStrings::GetSingleton()->bIsSynced, bIsSynced) && bIsSynced) {
			return false;
		}

		return UndoPitch(a_actor->data.angle.x, a_translation.y, a_translation.z);
	}

	void FixPitchTransHandler::Hook_ConvertMoveDirToTranslation(const RE::NiPoint3& a_angle, RE::NiPoint3& a_outDirection, RE::Actor* a_actor)
	{
		ConvertMoveDirToTranslation(a_angle, a_outDirection);
		RevertPitchRotation(a_actor, a_outDirection);
	}

	bool AttackMagnetismHandler::ShouldDisableMovementMagnetism(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}

		auto settings = AMFSettings::GetSingleton();
		static const RE::BSFixedString forceMoveMagnetism{ "AMF_bForceMoveMagnetism" };
		static const RE::BSFixedString forbidMoveMagnetism{ "AMF_bForbidMoveMagnetism" };
		bool bForceMoveMagnetism = false;
		if (a_actor->GetGraphVariableBool(forceMoveMagnetism, bForceMoveMagnetism) && bForceMoveMagnetism) {
			return false;
		}

		bool bForbidMoveMagnetism = false;
		if (a_actor->GetGraphVariableBool(forbidMoveMagnetism, bForbidMoveMagnetism) && bForbidMoveMagnetism) {
			return true;
		}

		const auto movMagMode = a_actor->IsPlayerRef() ?
			settings->playerMovementMagnetismMode.load(std::memory_order_relaxed) :
			settings->npcMovementMagnetismMode.load(std::memory_order_relaxed);
		switch (movMagMode) {
		case MovMagMode::kGlobalEnable:
			return false;

		case MovMagMode::kDynamic:
			return AMFSettings::HasAnimMotionData(a_actor);

		case MovMagMode::kGlobalDisable:
			return true;

		default:
			return false;
		}
	}
	void AttackMagnetismHandler::PlayerRotateMagnetismHook::UpdateMagnetism(RE::PlayerCharacter* a_player, float a_delta, RE::NiPoint3& a_translation, float& a_rotationZ)
	{
		if (!AMFSettings::GetSingleton()->disablePlayerRotationMagnetism.load(std::memory_order_relaxed)) {
			return func(a_player, a_delta, a_translation, a_rotationZ);
		}
	}

	bool AttackMagnetismHandler::MovementMagnetismHook::Hook_IsStartingMeleeAttack(RE::Actor* a_actor)
	{
		if (ShouldDisableMovementMagnetism(a_actor)) {
			return false;
		}

		return func(a_actor);
	}

	bool PushCharacterHandler::ShouldPreventAttackPushing(RE::Actor* a_pusher, RE::Actor* a_target)
	{
		if (!a_pusher || !a_target || !a_pusher->IsAttacking() || !a_pusher->IsMovementAnimationDriven())
			return false;

		const auto pusherHandle = a_pusher->GetActorRuntimeData().currentCombatTarget;
		auto combatTarg = pusherHandle ? pusherHandle.get() : nullptr;

		if (!combatTarg || (a_target != combatTarg.get() && !a_target->GetMountedBy(combatTarg))) {
			return false;
		}
		return AttackMagnetismHandler::ShouldDisableMovementMagnetism(a_pusher);
	}

	bool PushCharacterHandler::ShouldPreventAttackPushing(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target)
	{
		auto pusherActor = GetActor(a_pusher);
		if (pusherActor) {
			auto targetActor = GetActor(a_target);
			if (targetActor)
				return ShouldPreventAttackPushing(pusherActor->As<RE::Actor>(), targetActor->As<RE::Actor>());
		}

		return false;
	}

	RE::TESObjectREFRPtr PushCharacterHandler::GetActor(RE::bhkCharacterController* a_charCtrl)
	{
		auto rigidBody = a_charCtrl ? a_charCtrl->GetRigidBody() : nullptr;
		if (!rigidBody)
			return nullptr;

		const auto charCollisionFilterInfo = rigidBody->collidable.GetCollisionLayer();
		if (charCollisionFilterInfo != RE::COL_LAYER::kCharController)
			return nullptr;

		RE::TESObjectREFRPtr objRef(rigidBody->GetUserData());
		if (!IsValidActor(objRef))
			return nullptr;

		return objRef;
	}

	void PushCharacterHandler::ProxyPushProxyHandler::Hook_PushTargetCharacter(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target, RE::hkContactPoint* a_contactPoint)
	{
		if (ShouldPreventAttackPushing(a_pusher, a_target)) {
			return;
		}

		func(a_pusher, a_target, a_contactPoint);
	}

	void PushCharacterHandler::ProxyPushRigidBodyHandler::Hook_PushTargetCharacter(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target, RE::hkContactPoint* a_contactPoint)
	{
		if (ShouldPreventAttackPushing(a_pusher, a_target)) {
			return;
		}
		func(a_pusher, a_target, a_contactPoint);
	}

	void PushCharacterHandler::RigidBodyPushProxyHandler::Hook_ProcessConstraintsCallback(RE::bhkCharProxyController* a_proxyCtrl, const RE::hkpCharacterProxy* a_proxy, const RE::hkArray<RE::hkpRootCdPoint>& a_manifold, RE::hkpSimplexSolverInput& a_input)
	{
		ProcessConstraintsCallback(a_proxyCtrl, a_proxy, a_manifold, a_input);

		if (!a_input.constraints || a_input.numConstraints <= 0 || a_manifold.empty()) {
			return;
		}
		const auto proxyRef = GetActor(a_proxyCtrl);
		const auto target = proxyRef ? proxyRef->As<RE::Actor>() : nullptr;
		if (!target) {
			return;
		}
		const auto count = std::min(a_manifold.size(), a_input.numConstraints);
		for (std::int32_t i = 0; i < count; i++) {
			const RE::hkpRootCdPoint& rootPoint = a_manifold[i];
			const RE::hkpCollidable* rootCollidableB = rootPoint.rootCollidableB;

			if (!rootCollidableB || rootCollidableB->GetCollisionLayer() != RE::COL_LAYER::kCharController)
				continue;

			if (static_cast<RE::hkpWorldObject::BroadPhaseType>(rootCollidableB->broadPhaseHandle.type) != RE::hkpWorldObject::BroadPhaseType::kEntity)
				continue;

			RE::TESObjectREFRPtr attackerRef(RE::TESHavokUtilities::FindCollidableRef(*rootCollidableB));
			const auto attacker = IsValidActor(attackerRef) ? attackerRef->As<RE::Actor>() : nullptr;

			if (ShouldPreventAttackPushing(attacker, target)) {
				auto attackerCharCtrl = attacker->GetCharController();
				auto rigidBodyChar = attackerCharCtrl ? skyrim_cast<RE::bhkCharRigidBodyController*>(attackerCharCtrl) : nullptr;

				if (rigidBodyChar && charCtrlPlanes.Remember(rigidBodyChar, a_input.constraints[i].plane)) {
					a_input.constraints[i].velocity = { 0 };
				}
			}
		}
	}

	void PushCharacterHandler::RigidBodyPushProxyHandler::Hook_UpdateForAnimationAttack(RE::bhkCharacterController* a_charCtrl)
	{
		RE::NiPointer<RE::bhkCharacterController> controller(a_charCtrl);
		if (!controller) {
			return;
		}
		UpdateForAnimationAttack(controller.get());

		auto rigidCharCtrl = skyrim_cast<RE::bhkCharRigidBodyController*>(controller.get());
		if (!rigidCharCtrl)
			return;

		const auto normal = charCtrlPlanes.Take(rigidCharCtrl);
		if (!normal) {
			return;
		}
		RE::hkVector4 currentVelocity;
		rigidCharCtrl->GetLinearVelocityImpl(currentVelocity);
		const auto velDotNormal = currentVelocity.Dot3(*normal);
		if (velDotNormal > 0.0f) {
			currentVelocity = currentVelocity + *normal * (-velDotNormal);
			rigidCharCtrl->SetLinearVelocityImpl(currentVelocity);
		}
	}

	void PushCharacterHandler::RigidBodyPushProxyHandler::Hook_DeleteThis(RE::bhkCharRigidBodyController* a_charCtrl)
	{
		charCtrlPlanes.Erase(a_charCtrl);
		DeleteThis(a_charCtrl);
	}

	void PushCharacterHandler::ClearPendingPlanes()
	{
		RigidBodyPushProxyHandler::ClearPendingPlanes();
	}

	void PushCharacterHandler::RigidBodyPushRigidBodyHandler::Hook_PushTargetCharacter(RE::bhkCharacterController* a_pusher, RE::bhkCharacterController* a_target, RE::hkContactPoint* a_contactPoint)
	{
		if (ShouldPreventAttackPushing(a_pusher, a_target))
			return;

		PushTargetCharacter(a_pusher, a_target, a_contactPoint);
	}

	void PushCharacterHandler::RigidBodyPushRigidBodyHandler::Hook_ContactPointCallback(RE::FOCollisionListener* a_listener, const RE::hkpContactPointEvent& a_event)
	{
		const auto prop = a_event.contactPointProperties;
		if (!prop || prop->flags.any(RE::hkContactPointMaterial::Flag::kIsDisabled) || !a_event.contactMgr || !a_event.contactPoint) {
			return;
		}

		const auto rigidBodyA = a_event.bodies[0];
		const auto rigidBodyB = a_event.bodies[1];
		const auto islandA = rigidBodyA ? rigidBodyA->simulationIsland : nullptr;
		const auto islandB = rigidBodyB ? rigidBodyB->simulationIsland : nullptr;
		if (islandA && islandB) {
			RE::TESObjectREFRPtr APtr(rigidBodyA->GetUserData());
			RE::TESObjectREFRPtr BPtr(rigidBodyB->GetUserData());

			auto attacker = IsValidActor(APtr) ? APtr->As<RE::Actor>() : nullptr;
			auto target = IsValidActor(BPtr) ? BPtr->As<RE::Actor>() : nullptr;

			if (ShouldPreventAttackPushing(attacker, target)) {
				rigidBodyB->responseModifierFlags |= RE::hkpResponseModifier::Flags::kMassScaling;
				RE::hkpAddModifierUtil::SetInvMassScalingForContact(a_event, rigidBodyB, { 0 });
			}
			if (ShouldPreventAttackPushing(target, attacker)) {
				rigidBodyA->responseModifierFlags |= RE::hkpResponseModifier::Flags::kMassScaling;
				RE::hkpAddModifierUtil::SetInvMassScalingForContact(a_event, rigidBodyA, { 0 });
			}
		}

		ContactPointCallback(a_listener, a_event);
	}
}
