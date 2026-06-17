/**
 * AIControlAdapterGlaUsaStrategyManager.cpp
 *
 * Phase 14 GLA-vs-USA strategy policy.
 */

#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterGlaUsaStrategyManager.h"

#include <algorithm>

AIControlAdapterGlaUsaStrategyResult AIControlAdapterGlaUsaStrategyManager::Evaluate(
	const AIControlAdapterGlaUsaStrategyInput& input) const
{
	AIControlAdapterGlaUsaStrategyResult result;
	if (!input.enemyUsaDetected)
	{
		return result;
	}

	result.active = true;
	result.tunnelRole = "logistics_ambush_anchor";
	result.tunnelMissilesSatisfyArmor = false;
	result.reason = "usa_enemy_detected";

	if (input.enemyUsaWmdTargets > 0)
	{
		result.pressure = input.enemyArmorThreats > 0 || input.enemyAirThreats > 0 ? "mixed_wmd" : "wmd";
		result.wmdConstructionDiagnosticNeeded = true;
		result.wmdConstructionReason = "enemy_usa_wmd_detected";
	}
	else if (input.enemyMixedThreats > 0 || (input.enemyArmorThreats > 0 && input.enemyAirThreats > 0))
	{
		result.pressure = "mixed";
	}
	else if (input.enemyArmorThreats > 0)
	{
		result.pressure = "armor";
	}
	else if (input.enemyAirThreats > 0)
	{
		result.pressure = "air";
	}
	else
	{
		result.pressure = "unknown";
	}

	const int pressureScale = std::max(1, input.criticalZoneCount);
	const int armorFloor = 8 + (pressureScale * 2);
	const int airFloor = 10 + pressureScale;
	if (input.enemyArmorThreats > 0 || input.enemyMixedThreats > 0 || input.enemyUsaWmdTargets > 0)
	{
		result.scorpionFloor = armorFloor;
		result.quadFloor = std::max(6, airFloor - 2);
	}
	if (input.enemyAirThreats > 0 || input.enemyMixedThreats > 0)
	{
		result.quadFloor = std::max(result.quadFloor, airFloor);
		result.scorpionFloor = std::max(result.scorpionFloor, 6);
	}
	if (result.scorpionFloor == 0 && result.quadFloor == 0)
	{
		result.scorpionFloor = 6;
		result.quadFloor = 6;
	}

	const int effectiveScorpions = std::max(0, input.scorpions) + std::max(0, input.queuedScorpions);
	const int effectiveQuads = std::max(0, input.quads) + std::max(0, input.queuedQuads);
	result.preferScorpionProduction =
		input.readyArmsDealers > 0
		&& effectiveScorpions < result.scorpionFloor
		&& (input.enemyArmorThreats > 0 || input.enemyMixedThreats > 0 || input.enemyUsaWmdTargets > 0);
	result.preferQuadProduction =
		input.readyArmsDealers > 0
		&& !result.preferScorpionProduction
		&& effectiveQuads < result.quadFloor;

	if (input.armyCount >= 55 && input.activeAttackTasks <= 0 && input.activeDefenseTasks > 0 && !input.mainBaseCritical)
	{
		result.preserveAttackGroup = true;
		result.reservedStrikeGroup = std::min(24, std::max(10, input.armyCount / 5));
	}

	result.prioritizeProducerRecovery =
		input.producerSpineBroken
		|| (input.readyArmsDealers <= 0 && input.money >= input.reserveCash + 2500u)
		|| (input.readyBarracks <= 0 && input.money >= input.reserveCash + 1000u);

	const unsigned int cashFloat = input.money > input.reserveCash ? input.money - input.reserveCash : 0u;
	const int anchorCount = std::max(0, input.tunnels) + std::max(0, input.stingers);
	result.camouflageDesired =
		input.completedPalaces > 0
		&& anchorCount >= 8
		&& (input.exposedExpansionZones >= 2 || input.enemyUsaWmdTargets > 0 || input.enemyArmorThreats > 0);
	if (!result.camouflageDesired)
	{
		if (input.completedPalaces <= 0)
		{
			result.camouflageReason = "palace_missing";
		}
		else if (anchorCount < 8)
		{
			result.camouflageReason = "anchor_count_low";
		}
		else
		{
			result.camouflageReason = "low_payoff";
		}
	}
	else if (input.producerSpineBroken)
	{
		result.camouflageReason = "producer_spine_priority";
	}
	else if (input.mainBaseCritical || input.collapseImminent)
	{
		result.camouflageReason = "critical_defense_priority";
	}
	else if (cashFloat < 7000u)
	{
		result.camouflageReason = "cash_float_low";
	}
	else if (input.readyArmsDealers <= 0)
	{
		result.camouflageReason = "arms_dealer_missing";
	}
	else
	{
		result.camouflageSpendAllowed = true;
		result.camouflageReason = "usa_anchor_multiplier";
	}

	const int effectiveTechnicals = std::max(0, input.technicals) + std::max(0, input.queuedTechnicals);
	result.workerMobilityDesired =
		input.remoteBuildGap > 0
		&& input.workers >= 4
		&& input.farthestRemoteBuildDistance >= 1200.0f;
	if (result.workerMobilityDesired)
	{
		result.desiredShuttleTechnicals = input.remoteBuildGap >= 4 ? 2 : 1;
		result.protectedShuttleTechnicals = result.desiredShuttleTechnicals;
		result.shuttleTechnicalProductionNeeded =
			input.readyArmsDealers > 0
			&& effectiveTechnicals < result.desiredShuttleTechnicals;
		if (effectiveTechnicals >= result.desiredShuttleTechnicals)
		{
			result.workerMobilityMode = "technical";
			result.workerMobilityReason = "remote_build_distance";
			result.shuttleReservationReason = "reserved_for_worker_mobility";
		}
		else if (input.tunnels >= 2)
		{
			result.workerMobilityMode = "mixed";
			result.workerMobilityReason = "needs_more_technicals";
			result.shuttleReservationReason = "needs_more_technicals";
		}
		else
		{
			result.workerMobilityMode = "walk";
			result.workerMobilityReason = "technical_missing";
			result.shuttleReservationReason = "technical_missing";
		}
	}
	else if (input.remoteBuildGap <= 0)
	{
		result.workerMobilityReason = "no_remote_build_gap";
	}
	else if (input.workers < 4)
	{
		result.workerMobilityReason = "worker_count_low";
	}
	else
	{
		result.workerMobilityReason = "distance_short";
	}

	return result;
}
