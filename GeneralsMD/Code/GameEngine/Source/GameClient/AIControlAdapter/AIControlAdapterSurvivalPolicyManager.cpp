#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterSurvivalPolicyManager.h"

AIControlAdapterSurvivalPolicyResult AIControlAdapterSurvivalPolicyManager::Evaluate(
	const AIControlAdapterSurvivalPolicyInput& input) const
{
	AIControlAdapterSurvivalPolicyResult result;
	const bool reserveDepleted = input.money < input.reserveCash;
	result.wmdCrisis = input.enemyWmdTargets > 0 && input.readyScudStorms <= 0;
	result.producerSpineBroken = input.readyBarracks <= 0 || input.readyArmsDealers <= 0;
	result.collapseImminent =
		(input.criticalZoneCount >= 2 && input.activeCombatTasks <= 0) ||
		(input.criticalZoneCount >= 3 && input.defenseReserveDeficits >= 6);
	result.blockExposedWmdFoundations = result.collapseImminent || (result.wmdCrisis && reserveDepleted);

	if (result.collapseImminent)
	{
		result.state = "collapse_imminent";
		result.priority = "emergency_survival";
		result.reason = input.activeCombatTasks <= 0 ? "critical_zones_no_combat_tasks" : "critical_zone_defense_deficits";
		return result;
	}
	if (result.producerSpineBroken)
	{
		result.state = "producer_spine_broken";
		result.priority = "producer_recovery";
		result.reason = input.readyArmsDealers <= 0 ? "arms_dealer_missing" : "barracks_missing";
		return result;
	}
	if (result.wmdCrisis)
	{
		result.state = "wmd_crisis";
		result.priority = "protected_wmd_response";
		result.reason = "enemy_wmd_no_ready_scud";
		return result;
	}
	if (input.criticalZoneCount > 0 || input.defenseReserveDeficits > 0)
	{
		result.state = "under_pressure";
		result.priority = "stabilize_front";
		result.reason = input.criticalZoneCount > 0 ? "critical_zone_pressure" : "defense_reserve_deficit";
		return result;
	}

	return result;
}
