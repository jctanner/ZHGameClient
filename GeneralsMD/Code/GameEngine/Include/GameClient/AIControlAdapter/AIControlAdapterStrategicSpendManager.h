/**
 * AIControlAdapterStrategicSpendManager.h
 *
 * Central strategic spend gate for autonomy managers. This wraps the existing
 * policy evaluator with a snapshot/plan boundary so the adapter no longer owns
 * category-specific spend wiring inline.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <vector>

struct AIControlAdapterStrategicSpendSnapshot
{
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	int currentZones = 0;
	int developedZones = 0;
	int desiredZones = 0;
	int completedMarkets = 0;
	int healthyMarketsInProgress = 0;
	int staleMarketFoundations = 0;
	int staleStrategicFoundations = 0;
	int activeLocalEnemies = 0;
	int activeWmdThreats = 0;
	int armySize = 0;
	int armyCap = 0;
	int quads = 0;
	int buggies = 0;
	int scorpions = 0;
	bool mainBaseCritical = false;
	bool emergencySurvivalActive = false;
	bool expansionUrgent = false;
	bool incomeCritical = false;
	bool reserveDepleted = false;
};

struct AIControlAdapterStrategicSpendRequest
{
	StrategicSpendCategory category = StrategicSpendCategory::LuxuryBaseline;
	unsigned int requestCost = 0u;
};

struct AIControlAdapterStrategicSpendRecord
{
	StrategicSpendCategory category = StrategicSpendCategory::LuxuryBaseline;
	unsigned int requestCost = 0u;
	AIControlAdapterStrategicSpendDecision decision;
};

struct AIControlAdapterStrategicSpendPlan
{
	std::vector<AIControlAdapterStrategicSpendRecord> records;
	AIControlAdapterStrategicSpendDecision expansion;
	AIControlAdapterStrategicSpendDecision economyRecovery;
	AIControlAdapterStrategicSpendDecision economyGrowth;
	AIControlAdapterStrategicSpendDecision techPrerequisite;
	AIControlAdapterStrategicSpendDecision staticDefense;
	const char* lastAllowedCategory = nullptr;
	const char* lastBlockedReason = nullptr;
	unsigned int protectedCash = 0u;
};

class AIControlAdapterStrategicSpendManager
{
public:
	AIControlAdapterStrategicSpendDecision Evaluate(
		const AIControlAdapterStrategicSpendSnapshot& snapshot,
		StrategicSpendCategory category,
		unsigned int requestCost) const;

	AIControlAdapterStrategicSpendPlan EvaluateMacroPlan(
		const AIControlAdapterStrategicSpendSnapshot& snapshot,
		unsigned int expansionCost,
		unsigned int economyRecoveryCost,
		unsigned int economyGrowthCost,
		unsigned int techPrerequisiteCost,
		unsigned int staticDefenseCost) const;

private:
	AIControlAdapterStrategicSpendInput BuildInput(
		const AIControlAdapterStrategicSpendSnapshot& snapshot,
		unsigned int requestCost) const;

	void AddRecord(
		AIControlAdapterStrategicSpendPlan& plan,
		const AIControlAdapterStrategicSpendSnapshot& snapshot,
		StrategicSpendCategory category,
		unsigned int requestCost) const;
};
