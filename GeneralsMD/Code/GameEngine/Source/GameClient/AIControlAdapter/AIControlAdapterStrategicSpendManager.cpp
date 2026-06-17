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
