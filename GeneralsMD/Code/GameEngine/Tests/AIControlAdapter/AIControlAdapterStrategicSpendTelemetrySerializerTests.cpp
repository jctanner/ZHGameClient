#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendTelemetrySerializer.h"

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

	AIControlAdapterStrategicSpendDecision makeDecision(bool allowed, const char* reason)
	{
		AIControlAdapterStrategicSpendDecision decision;
		decision.allowed = allowed;
		decision.protectedCash = 10000u;
		decision.spendBudget = allowed ? 25000u : 0u;
		decision.batchLimit = allowed ? 2 : 0;
		decision.reason = reason;
		return decision;
	}
}

int main()
{
	const AIControlAdapterStrategicSpendTelemetrySerializer serializer;

	{
		const nlohmann::json telemetry = serializer.BuildDefaultTelemetry();
		expect(telemetry.value("protected_cash", -1) == 0, "Default telemetry should expose protected cash");
		expect(telemetry.value("last_allowed_category", std::string()) == "none", "Default telemetry should expose last allowed category");
		expect(telemetry.value("last_blocked_reason", std::string()) == "not_evaluated", "Default telemetry should expose blocked reason");
		expect(telemetry["categories"].is_object(), "Default telemetry should expose category object");
		expect(telemetry.value("market_foundations_no_builder", -1) == 0, "Default telemetry should expose no-builder foundations");
	}

	{
		nlohmann::json telemetry = serializer.BuildDefaultTelemetry();
		const AIControlAdapterStrategicSpendDecision decision = makeDecision(true, "allowed");
		serializer.RecordCategory(telemetry, StrategicSpendCategory::StaticDefense, 1200u, decision);
		expect(telemetry["categories"]["static_defense"].value("allowed", false), "Category telemetry should preserve allowed");
		expect(telemetry["categories"]["static_defense"].value("request_cost", 0u) == 1200u, "Category telemetry should preserve request cost");
		expect(telemetry.value("protected_cash", 0u) == 10000u, "Category telemetry should update protected cash");
		expect(telemetry.value("last_allowed_category", std::string()) == "static_defense", "Allowed category should update last allowed");
	}

	{
		nlohmann::json telemetry = serializer.BuildDefaultTelemetry();
		serializer.RecordCategory(telemetry, StrategicSpendCategory::EconomyRecovery, 2500u, makeDecision(false, "reserve_protected"));
		expect(!telemetry["categories"]["economy_recovery"].value("allowed", true), "Blocked category should preserve allowed flag");
		expect(telemetry.value("last_blocked_reason", std::string()) == "reserve_protected", "Blocked category should update blocked reason");
		serializer.RecordMarketFoundationCounts(telemetry, 3, 1, 2);
		expect(telemetry.value("healthy_market_foundations", 0) == 3, "Foundation telemetry should preserve healthy count");
		expect(telemetry.value("stale_market_foundations", 0) == 1, "Foundation telemetry should preserve stale market count");
		expect(telemetry.value("stale_strategic_foundations", 0) == 2, "Foundation telemetry should preserve stale strategic count");
	}

	{
		nlohmann::json telemetry = serializer.BuildDefaultTelemetry();
		AIControlAdapterStrategicSpendPlan plan;
		AIControlAdapterStrategicSpendRecord first;
		first.category = StrategicSpendCategory::Expansion;
		first.requestCost = 1800u;
		first.decision = makeDecision(true, "allowed");
		AIControlAdapterStrategicSpendRecord second;
		second.category = StrategicSpendCategory::LuxuryBaseline;
		second.requestCost = 5000u;
		second.decision = makeDecision(false, "urgent_expansion_priority");
		plan.records.push_back(first);
		plan.records.push_back(second);
		serializer.RecordPlan(telemetry, plan);
		expect(telemetry["categories"].contains("expansion"), "Plan telemetry should include first category");
		expect(telemetry["categories"].contains("luxury_baseline"), "Plan telemetry should include second category");
		expect(telemetry.value("last_allowed_category", std::string()) == "expansion", "Plan telemetry should preserve last allowed category");
		expect(telemetry.value("last_blocked_reason", std::string()) == "urgent_expansion_priority", "Plan telemetry should preserve last blocked reason");
	}

	{
		const AIControlAdapterStrategicSpendDecision decision = makeDecision(false, "reserve_protected");
		const std::string line = serializer.BuildPolicyLogLine({
			StrategicSpendCategory::LuxuryBaseline,
			14000u,
			10000u,
			&decision
		});
		expect(
			line == "strategic_spend_policy category=luxury_baseline allowed=0 money=14000 reserve=10000 protected_cash=10000 spend_budget=0 batch_limit=0 reason=reserve_protected",
			"Policy log should preserve category, money, reserve, and decision fields");
	}

	{
		const std::string line = serializer.BuildPolicyLogLine({
			StrategicSpendCategory::DefensiveWmd,
			22000u,
			10000u,
			nullptr
		});
		expect(
			line == "strategic_spend_policy category=defensive_wmd allowed=0 money=22000 reserve=10000 protected_cash=0 spend_budget=0 batch_limit=0 reason=not_evaluated",
			"Policy log should expose not-evaluated fallback without a decision");
	}

	std::cout << "AIControlAdapterStrategicSpendTelemetrySerializerTests passed\n";
	return 0;
}
