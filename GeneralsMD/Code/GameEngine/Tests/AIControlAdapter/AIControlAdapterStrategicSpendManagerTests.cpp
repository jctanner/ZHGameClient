#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendManager.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << "\n";
			std::exit(1);
		}
	}

	AIControlAdapterStrategicSpendSnapshot makeBaseSnapshot()
	{
		AIControlAdapterStrategicSpendSnapshot snapshot;
		snapshot.money = 12000u;
		snapshot.reserveCash = 10000u;
		snapshot.currentZones = 8;
		snapshot.developedZones = 5;
		snapshot.desiredZones = 30;
		snapshot.completedMarkets = 1;
		snapshot.healthyMarketsInProgress = 0;
		snapshot.staleMarketFoundations = 0;
		snapshot.staleStrategicFoundations = 0;
		snapshot.activeWmdThreats = 0;
		snapshot.armySize = 20;
		snapshot.armyCap = 30;
		snapshot.quads = 8;
		snapshot.buggies = 4;
		snapshot.scorpions = 6;
		snapshot.mainBaseCritical = false;
		snapshot.expansionUrgent = false;
		snapshot.incomeCritical = false;
		snapshot.reserveDepleted = false;
		return snapshot;
	}
}

int main()
{
	const AIControlAdapterStrategicSpendManager manager;

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 11000u;
		const AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::EconomyGrowth, 2500u);
		expect(!decision.allowed, "Economy growth should protect reserve when cash float is too low");
		expect(decision.reason == std::string("reserve_protected"), "Economy growth block reason should be reserve_protected");
	}

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 2000u;
		snapshot.reserveDepleted = true;
		snapshot.incomeCritical = true;
		const AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::EconomyRecovery, 2500u);
		expect(!decision.allowed, "Economy recovery should still require affordable cash");
		expect(decision.reason == std::string("cash_below_recovery_cost"), "Economy recovery should report low cash");
	}

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 1800u;
		snapshot.reserveCash = 10000u;
		snapshot.expansionUrgent = true;
		AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		// Urgency no longer spends through the shared reserve (pre-milestone budget starvation).
		expect(!decision.allowed, "Urgent expansion should not spend through the protected reserve");
		expect(decision.reason == std::string("recovery_cash_protected"), "Urgent expansion below reserve should report recovery cash protection");

		snapshot.money = 12000u;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(decision.allowed, "Urgent expansion should be allowed when funded above the reserve");
		expect(decision.reason == std::string("urgent_expansion_priority"), "Urgent expansion should preserve policy reason");
	}

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 10500u;
		const AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::StaticDefense, 1200u);
		expect(!decision.allowed, "Static defense should be blocked when protected cash is insufficient");
		expect(decision.reason == std::string("recovery_cash_protected"), "Static defense should report recovery cash protection");
	}

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 20000u;
		const AIControlAdapterStrategicSpendPlan plan =
			manager.EvaluateMacroPlan(snapshot, 1800u, 2500u, 2500u, 5000u, 1200u);
		expect(plan.records.size() == 5u, "Macro spend plan should evaluate all macro categories");
		expect(plan.expansion.allowed, "Macro spend plan should expose expansion decision");
		expect(plan.economyRecovery.allowed, "Macro spend plan should expose economy recovery decision");
		expect(plan.economyGrowth.allowed, "Macro spend plan should expose economy growth decision");
		expect(plan.techPrerequisite.allowed, "Macro spend plan should expose tech prerequisite decision");
		expect(plan.staticDefense.allowed, "Macro spend plan should expose static defense decision");
		expect(plan.lastAllowedCategory != nullptr, "Macro spend plan should record the last allowed category");
		expect(plan.protectedCash == 10000u, "Macro spend plan should expose protected cash");
	}

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 700u;
		snapshot.activeLocalEnemies = 8;
		snapshot.mainBaseCritical = true;
		snapshot.emergencySurvivalActive = true;
		const AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::EmergencyDefenseUnits, 700u);
		expect(decision.allowed, "Emergency defense should allow immediate critical spend when affordable");
		expect(decision.batchLimit == 3, "Emergency defense should expose critical batch limit");
		expect(decision.reason == std::string("main_base_critical_override"), "Emergency defense should preserve critical reason");
	}

	{
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 15000u;
		snapshot.activeWmdThreats = 1;
		snapshot.expansionUrgent = false;
		const AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::DefensiveWmd, 5000u);
		expect(decision.allowed, "Defensive WMD spend should be allowed for active WMD threat when protected");
		expect(decision.reason == std::string("defensive_wmd"), "Defensive WMD should preserve policy reason");
	}

	{
		// Economic profile under threat response: income deferral holds without a surplus.
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 98000u;
		snapshot.completedMarkets = 9;
		snapshot.profilePlan.active = true;
		snapshot.profilePlan.threatResponse = true;
		snapshot.profilePlan.deferIncomeUnderThreat = true;
		snapshot.profilePlan.incomeProtectedCash = 3000u;
		snapshot.profilePlan.growthProtectedCash = 6000u;
		snapshot.profilePlan.maxConcurrentMarkets = 1;
		snapshot.profilePlan.armyFloorMet = false;
		snapshot.profilePlan.reason = "threat_response";
		AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::EconomyGrowth, 2500u);
		expect(!decision.allowed, "Threat response without surplus should defer market growth");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(!decision.allowed, "Threat response without surplus or army floor should defer expansion");

		snapshot.profilePlan.surplusUnderThreat = true;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::EconomyGrowth, 2500u);
		expect(decision.allowed, "Surplus under threat should permit funded market growth");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(decision.allowed, "Surplus under threat should permit funded expansion");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::LuxuryBaseline, 1000u);
		expect(!decision.allowed, "Surplus under threat should keep luxury spending deferred");

		snapshot.healthyMarketsInProgress = 1;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::EconomyGrowth, 2500u);
		expect(!decision.allowed, "Surplus growth should keep the market concurrency limit");

		snapshot.healthyMarketsInProgress = 0;
		snapshot.mainBaseCritical = true;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::EconomyGrowth, 2500u);
		expect(!decision.allowed, "Critical main base pressure should defer surplus growth");
	}

	{
		// Economic opening reserve holds Palace + first market cash from non-income spend.
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 9000u;
		snapshot.completedMarkets = 0;
		snapshot.profilePlan.active = true;
		snapshot.profilePlan.threatResponse = false;
		snapshot.profilePlan.incomeProtectedCash = 6000u;
		snapshot.profilePlan.growthProtectedCash = 6000u;
		snapshot.profilePlan.maxConcurrentMarkets = 1;
		snapshot.profilePlan.openingIncomeReserve = 5000u;
		snapshot.profilePlan.reason = "quiet_economic_growth";
		AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::ProductionGrowth, 2500u);
		expect(!decision.allowed, "Opening reserve should hold production growth below reserve plus income cost");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(decision.allowed, "Expansion first: the opening reserve must not block supply expansion");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::TechPrerequisite, 2500u);
		expect(decision.allowed, "Opening reserve must not block the Palace tech prerequisite");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::EconomyRecovery, 2500u);
		expect(decision.allowed, "Opening reserve must not block the first Black Market");
		snapshot.profilePlan.openingIncomeReserve = 0u;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::ProductionGrowth, 2500u);
		expect(decision.allowed, "Without the opening reserve funded production growth proceeds");
	}

	{
		// Expansion first under pressure, gated by the wave-sized army floor.
		AIControlAdapterStrategicSpendSnapshot snapshot = makeBaseSnapshot();
		snapshot.money = 9000u;
		snapshot.completedMarkets = 1;
		snapshot.profilePlan.active = true;
		snapshot.profilePlan.threatResponse = true;
		snapshot.profilePlan.growthProtectedCash = 6000u;
		snapshot.profilePlan.armyFloor = 30;
		snapshot.profilePlan.armyFloorMet = true;
		snapshot.profilePlan.reason = "threat_response";
		AIControlAdapterStrategicSpendDecision decision =
			manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(decision.allowed, "Expansion should continue under threat when the army floor is met");
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::ProductionGrowth, 2500u);
		expect(!decision.allowed, "Production growth stays deferred under threat without surplus");
		snapshot.profilePlan.armyFloorMet = false;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(!decision.allowed, "Expansion should wait under threat while the army is below the floor");
		snapshot.profilePlan.armyFloorMet = true;
		snapshot.mainBaseCritical = true;
		decision = manager.Evaluate(snapshot, StrategicSpendCategory::Expansion, 1800u);
		expect(!decision.allowed, "A critical main base should pause expansion");
	}

	std::cout << "AIControlAdapterStrategicSpendManagerTests passed\n";
	return 0;
}
