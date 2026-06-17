#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildTelemetrySerializer.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
}

int main()
{
	const AIControlAdapterMacroBuildTelemetrySerializer serializer;

	{
		const nlohmann::json telemetry = serializer.BuildDefaultTelemetry();
		expect(telemetry.value("profile", std::string()) == "none", "Default telemetry should expose profile");
		expect(telemetry.value("cash_float", -1) == 0, "Default telemetry should expose cash float");
		expect(telemetry["intents"].is_array(), "Default telemetry should expose intents array");
		expect(telemetry["spend"].is_object(), "Default telemetry should expose spend object");
		expect(telemetry["selected"].value("reason", std::string()) == "not_evaluated", "Default telemetry should expose selected reason");
		expect(telemetry["result"].value("reason", std::string()) == "not_evaluated", "Default telemetry should expose result reason");
	}

	{
		AIControlAdapterMacroBuildPlan plan;
		AIControlAdapterMacroBuildIntent intent;
		intent.option.category = "static_defense";
		intent.option.command = "Game.BuildStingerSite";
		intent.option.reason = "frontier_floor";
		intent.option.priority = 80;
		intent.option.valid = true;
		intent.executor = AIControlAdapterMacroBuildExecutor::SpecificZone;
		intent.preferZone = true;
		intent.minCash = 1200u;
		intent.inProgress = 0;
		intent.maxInProgress = 1;
		intent.zoneIndex = 2;
		plan.intents.push_back(intent);
		plan.choice.index = 0;
		plan.choice.category = intent.option.category;
		plan.choice.command = intent.option.command;
		plan.choice.reason = intent.option.reason;
		plan.choice.priority = intent.option.priority;
		plan.telemetry.expansionMode = "urgent";
		plan.telemetry.expansionReason = "coverage_gap_high_cash";
		plan.telemetry.expansionCommand = "Game.BuildSupplyStashSmart";
		plan.telemetry.expansionShouldAttempt = true;
		plan.telemetry.expansionDecisionReason = "allowed";
		plan.telemetry.expansionIsUrgent = true;
		plan.telemetry.coverageExpansionUrgent = true;
		plan.telemetry.preferRemoteSupplyExpansion = true;
		plan.telemetry.remoteSupplyZoneCount = 1;
		plan.telemetry.desiredRemoteSupplyZones = 3;
		plan.telemetry.supplyFootprintRadius = 1400.0f;
		plan.telemetry.zoneSeedCommand = "Game.BuildTunnelNetwork";
		plan.telemetry.zoneSeedPackageStage = "tunnel";
		plan.telemetry.zoneSeedReason = "remote_stash";
		plan.telemetry.zoneSeedPriority = 70;

		AIControlAdapterStrategicSpendPlan spendPlan;
		AIControlAdapterStrategicSpendRecord spendRecord;
		spendRecord.category = StrategicSpendCategory::StaticDefense;
		spendRecord.requestCost = 1200u;
		spendRecord.decision.allowed = true;
		spendRecord.decision.protectedCash = 10000u;
		spendRecord.decision.spendBudget = 25000u;
		spendRecord.decision.batchLimit = 1;
		spendRecord.decision.reason = "allowed";
		spendPlan.records.push_back(spendRecord);

		AIControlAdapterMacroBuildTelemetryInput input;
		input.profile = "sprawl_balanced";
		input.money = 50000u;
		input.reserveCash = 10000u;
		input.cashAboveReserve = 40000u;
		input.currentZones = 7;
		input.developedZones = 4;
		input.desiredZones = 30;
		input.zoneGap = 23;
		input.activeZoneAnchor = 42u;
		input.activeZoneThreatened = true;
		input.remoteZoneNeedsFollowup = true;
		input.macroBuildPlan = &plan;
		input.spendPlan = &spendPlan;

		const nlohmann::json telemetry = serializer.BuildTelemetry(input);
		expect(telemetry.value("profile", std::string()) == "sprawl_balanced", "Telemetry should preserve profile");
		expect(telemetry.value("money", 0u) == 50000u, "Telemetry should preserve money");
		expect(telemetry.value("zone_gap", 0) == 23, "Telemetry should preserve zone gap");
		expect(telemetry["expansion"].value("mode", std::string()) == "urgent", "Telemetry should preserve expansion mode");
		expect(telemetry["zone_seed"].value("command", std::string()) == "Game.BuildTunnelNetwork", "Telemetry should preserve zone seed command");
		expect(telemetry["intents"].size() == 1, "Telemetry should include intents");
		expect(telemetry["intents"][0].value("executor", std::string()) == "specific_zone", "Telemetry should serialize executor");
		expect(telemetry["selected"].value("command", std::string()) == "Game.BuildStingerSite", "Telemetry should preserve selected command");
		expect(telemetry["spend"]["static_defense"].value("request_cost", 0u) == 1200u, "Telemetry should preserve spend request cost");
		expect(telemetry["result"].value("reason", std::string()) == "not_attempted", "Live telemetry should default result to not attempted");

		const std::vector<std::string> lines = serializer.BuildExpansionLogLines({
			&plan.telemetry,
			10000u,
			42u,
			true
		});
		expect(lines.size() == 4, "Expansion log builder should include zone seed line when requested");
		expect(lines[0].find("zone_expansion_policy mode=urgent") == 0, "Expansion policy log should preserve grep prefix");
		expect(lines[1].find("zone_expansion_request command=Game.BuildSupplyStashSmart") == 0, "Expansion request log should preserve command");
		expect(lines[2].find("sprawl_expansion_throughput current=0") == 0, "Throughput log should preserve grep prefix");
		expect(lines[2].find("reserve=10000") != std::string::npos, "Throughput log should include reserve");
		expect(lines[3].find("sprawl_zone_seed zone=42 command=Game.BuildTunnelNetwork") == 0, "Zone seed log should preserve anchor and command");
	}

	{
		AIControlAdapterStaticDefensePolicyResult policy;
		policy.desiredTunnels = 2;
		policy.desiredStingers = 3;
		policy.effectiveTunnels = 1;
		policy.effectiveStingers = 2;
		policy.shouldBuildTunnel = true;
		policy.shouldBuildStinger = false;
		policy.role = "frontier";
		policy.reason = "frontier_floor";
		const AIControlAdapterStaticDefensePolicyTelemetryInput input{ 77u, &policy, 1 };
		const nlohmann::json telemetry = serializer.BuildStaticDefensePolicyTelemetry(input);
		const std::string line = serializer.BuildStaticDefensePolicyLogLine(input);
		expect(telemetry.value("zone", 0u) == 77u, "Static defense telemetry should preserve zone");
		expect(telemetry.value("role", std::string()) == "frontier", "Static defense telemetry should preserve role");
		expect(telemetry.value("desired_stingers", 0) == 3, "Static defense telemetry should preserve desired stingers");
		expect(line == "static_defense_policy zone=77 role=frontier tunnels=1/2 stingers=2/3 in_progress=1 reason=frontier_floor", "Static defense log should preserve existing shape");
	}

	{
		AIControlAdapterPalaceRedundancyResult policy;
		policy.desiredZonePalaces = 1;
		policy.spendAllowed = true;
		policy.shouldBuild = true;
		policy.role = "anchor";
		policy.reason = "anchor_redundancy";
		const AIControlAdapterPalaceRedundancyTelemetryInput input{ 88u, &policy, 0, 1 };
		const nlohmann::json telemetry = serializer.BuildPalaceRedundancyTelemetry(input);
		const std::string line = serializer.BuildPalaceRedundancyLogLine(input);
		expect(telemetry.value("zone", 0u) == 88u, "Palace redundancy telemetry should preserve zone");
		expect(telemetry.value("desired", 0) == 1, "Palace redundancy telemetry should preserve desired count");
		expect(telemetry.value("spend_allowed", false), "Palace redundancy telemetry should preserve spend allowance");
		expect(line == "palace_redundancy_policy zone=88 role=anchor live=0 desired=1 in_progress=1 spend_allowed=1 reason=anchor_redundancy", "Palace redundancy log should preserve existing shape");
	}

	{
		AIControlAdapterBrutalPressureResult policy;
		policy.chosenPriority = "emergency_survival";
		policy.reason = "main_base_pressure";
		const AIControlAdapterBrutalPressureTelemetryInput input{
			23,
			true,
			4,
			2,
			1,
			3,
			5,
			true,
			42000u,
			&policy
		};
		const nlohmann::json telemetry = serializer.BuildBrutalPressureTelemetry(input);
		const std::string line = serializer.BuildBrutalPressureLogLine(input);
		expect(telemetry.value("expansion_gap", 0) == 23, "Brutal pressure telemetry should preserve expansion gap");
		expect(telemetry.value("main_under_pressure", false), "Brutal pressure telemetry should preserve main pressure");
		expect(telemetry.value("mobile_siege_threats", 0) == 5, "Brutal pressure telemetry should preserve siege threats");
		expect(telemetry.value("cash_float", 0u) == 42000u, "Brutal pressure telemetry should preserve cash float");
		expect(telemetry.value("chosen_priority", std::string()) == "emergency_survival", "Brutal pressure telemetry should preserve priority");
		expect(telemetry.value("reason", std::string()) == "main_base_pressure", "Brutal pressure telemetry should preserve reason");
		expect(line == "brutal_pressure_policy expansion_gap=23 main_under_pressure=1 static_defense_gap=4 garrison_gap=2 local_worker_gap=1 stale_foundations=3 mobile_siege_threats=5 reserve_protected=1 cash_float=42000 chosen_priority=emergency_survival reason=main_base_pressure", "Brutal pressure log should preserve existing shape");
	}

	{
		const AIControlAdapterBrutalPressureTelemetryInput input;
		const nlohmann::json telemetry = serializer.BuildBrutalPressureTelemetry(input);
		const std::string line = serializer.BuildBrutalPressureLogLine(input);
		expect(telemetry.value("chosen_priority", std::string()) == "not_evaluated", "Missing brutal pressure policy should expose default priority");
		expect(telemetry.value("reason", std::string()) == "not_evaluated", "Missing brutal pressure policy should expose default reason");
		expect(line.find("chosen_priority=not_evaluated reason=not_evaluated") != std::string::npos, "Missing brutal pressure policy log should expose defaults");
	}

	{
		AIControlAdapterMacroBuildIntent intent;
		intent.option.category = "producer_growth";
		intent.option.command = "Game.BuildBarracksSmart";
		intent.option.priority = 30;
		intent.option.valid = false;
		intent.option.reason = "wait_money";
		intent.minCash = 10000u;
		intent.inProgress = 1;
		intent.maxInProgress = 1;
		const std::string line = serializer.BuildIntentLogLine(intent, 9000u);
		expect(line == "macro_build_intent category=producer_growth command=Game.BuildBarracksSmart priority=30 valid=0 money=9000 min_cash=10000 in_progress=1 max_in_progress=1 reason=wait_money", "Intent log should preserve existing shape");
	}

	{
		AIControlAdapterMacroBuildDispatchResult result;
		result.category = "static_defense";
		result.command = "Game.BuildStingerSite";
		result.priority = 80;
		result.issued = true;
		result.reason = "ok";
		const std::string line = serializer.BuildDispatchResultLogLine(result);
		expect(line == "macro_build_intent_result category=static_defense command=Game.BuildStingerSite priority=80 issued=1 reason=ok", "Dispatch result log should preserve existing shape");
	}

	std::cout << "AIControlAdapterMacroBuildTelemetrySerializerTests passed\n";
	return 0;
}
