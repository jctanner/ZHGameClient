#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendManager.h"

AIControlAdapterStrategicSpendInput AIControlAdapterStrategicSpendManager::BuildInput(
	const AIControlAdapterStrategicSpendSnapshot& snapshot,
	unsigned int requestCost) const
{
	AIControlAdapterStrategicSpendInput input;
	input.money = snapshot.money;
	input.reserveCash = snapshot.reserveCash;
	input.requestCost = requestCost;
	input.currentZones = snapshot.currentZones;
	input.developedZones = snapshot.developedZones;
	input.desiredZones = snapshot.desiredZones;
	input.completedMarkets = snapshot.completedMarkets;
	input.healthyMarketsInProgress = snapshot.healthyMarketsInProgress;
	input.staleMarketFoundations = snapshot.staleMarketFoundations;
	input.staleStrategicFoundations = snapshot.staleStrategicFoundations;
	input.activeLocalEnemies = snapshot.activeLocalEnemies;
	input.activeWmdThreats = snapshot.activeWmdThreats;
	input.armySize = snapshot.armySize;
	input.armyCap = snapshot.armyCap;
	input.quads = snapshot.quads;
	input.buggies = snapshot.buggies;
	input.scorpions = snapshot.scorpions;
	input.mainBaseCritical = snapshot.mainBaseCritical;
	input.emergencySurvivalActive = snapshot.emergencySurvivalActive;
	input.expansionUrgent = snapshot.expansionUrgent;
	input.incomeCritical = snapshot.incomeCritical;
	input.reserveDepleted = snapshot.reserveDepleted;
	return input;
}

AIControlAdapterStrategicSpendDecision AIControlAdapterStrategicSpendManager::Evaluate(
	const AIControlAdapterStrategicSpendSnapshot& snapshot,
	StrategicSpendCategory category,
	unsigned int requestCost) const
{
	if (snapshot.profilePlan.active)
	{
		const auto& plan = snapshot.profilePlan;
		AIControlAdapterStrategicSpendDecision decision;
		decision.protectedCash = plan.growthProtectedCash;
		decision.batchLimit = 1;
		bool permitted = true;
		switch (category)
		{
		case StrategicSpendCategory::EconomyRecovery:
		case StrategicSpendCategory::EconomyGrowth:
			decision.protectedCash = plan.incomeProtectedCash;
			permitted = snapshot.healthyMarketsInProgress < plan.maxConcurrentMarkets
				&& (!plan.threatResponse || !plan.deferIncomeUnderThreat || snapshot.completedMarkets == 0);
			break;
		case StrategicSpendCategory::LocalWorkerRecovery:
			decision.protectedCash = plan.workerProtectedCash;
			break;
		case StrategicSpendCategory::ProducerRecovery:
			decision.protectedCash = 1000u;
			break;
		case StrategicSpendCategory::EmergencyDefenseUnits:
		case StrategicSpendCategory::CounterbatteryUnits:
			decision.protectedCash = plan.combatProtectedCash;
			permitted = snapshot.armySize < plan.armyTarget;
			decision.batchLimit = plan.threatResponse ? 3 : 1;
			break;
		case StrategicSpendCategory::DefensiveWmd:
			// Preserve money for local counters when the standing defense is thin.
			permitted = snapshot.activeWmdThreats > 0 && (!plan.threatResponse || snapshot.armySize >= 12);
			break;
		case StrategicSpendCategory::StaticDefense:
			permitted = !plan.threatResponse || snapshot.armySize >= 12;
			break;
		case StrategicSpendCategory::ProductionGrowth:
		case StrategicSpendCategory::Expansion:
		case StrategicSpendCategory::TechPrerequisite:
		case StrategicSpendCategory::LuxuryBaseline:
			permitted = !plan.threatResponse;
			break;
		}
		decision.spendBudget = snapshot.money > decision.protectedCash ? snapshot.money - decision.protectedCash : 0u;
		decision.allowed = permitted && requestCost <= decision.spendBudget;
		decision.reason = !permitted ? "profile_response_priority"
			: decision.allowed ? plan.reason : "profile_reserve_protected";
		return decision;
	}
	return AIControlAdapterEvaluateStrategicSpend(category, BuildInput(snapshot, requestCost));
}

void AIControlAdapterStrategicSpendManager::AddRecord(
	AIControlAdapterStrategicSpendPlan& plan,
	const AIControlAdapterStrategicSpendSnapshot& snapshot,
	StrategicSpendCategory category,
	unsigned int requestCost) const
{
	AIControlAdapterStrategicSpendRecord record;
	record.category = category;
	record.requestCost = requestCost;
	record.decision = Evaluate(snapshot, category, requestCost);
	plan.protectedCash = record.decision.protectedCash;
	if (record.decision.allowed)
	{
		plan.lastAllowedCategory = AIControlAdapterStrategicSpendCategoryName(category);
	}
	else
	{
		plan.lastBlockedReason = record.decision.reason;
	}

	switch (category)
	{
	case StrategicSpendCategory::Expansion:
		plan.expansion = record.decision;
		break;
	case StrategicSpendCategory::EconomyRecovery:
		plan.economyRecovery = record.decision;
		break;
	case StrategicSpendCategory::EconomyGrowth:
		plan.economyGrowth = record.decision;
		break;
	case StrategicSpendCategory::TechPrerequisite:
		plan.techPrerequisite = record.decision;
		break;
	case StrategicSpendCategory::StaticDefense:
		plan.staticDefense = record.decision;
		break;
	default:
		break;
	}
	plan.records.push_back(record);
}

AIControlAdapterStrategicSpendPlan AIControlAdapterStrategicSpendManager::EvaluateMacroPlan(
	const AIControlAdapterStrategicSpendSnapshot& snapshot,
	unsigned int expansionCost,
	unsigned int economyRecoveryCost,
	unsigned int economyGrowthCost,
	unsigned int techPrerequisiteCost,
	unsigned int staticDefenseCost) const
{
	AIControlAdapterStrategicSpendPlan plan;
	AddRecord(plan, snapshot, StrategicSpendCategory::Expansion, expansionCost);
	AddRecord(plan, snapshot, StrategicSpendCategory::EconomyRecovery, economyRecoveryCost);
	AddRecord(plan, snapshot, StrategicSpendCategory::EconomyGrowth, economyGrowthCost);
	AddRecord(plan, snapshot, StrategicSpendCategory::TechPrerequisite, techPrerequisiteCost);
	AddRecord(plan, snapshot, StrategicSpendCategory::StaticDefense, staticDefenseCost);
	return plan;
}
