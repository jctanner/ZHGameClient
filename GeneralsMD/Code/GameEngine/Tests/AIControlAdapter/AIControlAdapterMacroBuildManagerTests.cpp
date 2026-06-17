#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildManager.h"

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

	AIControlAdapterMacroBuildSnapshot makeBaseSnapshot()
	{
		AIControlAdapterMacroBuildSnapshot snapshot;
		snapshot.isSprawlStyle = true;
		snapshot.isBalancedSprawl = true;
		snapshot.hasActiveZone = true;
		snapshot.money = 50000u;
		snapshot.reserveCash = 10000u;
		snapshot.blackMarketCost = 2500u;
		snapshot.stashZoneCount = 10;
		snapshot.desiredZoneCount = 30;
		snapshot.urgentZoneGapThreshold = 5;
		snapshot.expansionHighCashFloatThreshold = 10000u;
		snapshot.supplyStashesInProgress = 0;
		snapshot.maxConcurrentExpansionStashes = 3;
		snapshot.supplyBuildCooldownReady = true;
		snapshot.expansionSpend.allowed = true;
		snapshot.expansionSpend.reason = "allowed";
		snapshot.marketRecoverySpend.allowed = true;
		snapshot.marketRecoverySpend.reason = "allowed";
		snapshot.marketGrowthSpend.allowed = true;
		snapshot.marketGrowthSpend.reason = "allowed";
		snapshot.hasCompletedPalace = true;
		snapshot.totalSupplyStashes = 10;
		snapshot.localSupplyEstablished = true;
		snapshot.totalBarracks = 4;
		snapshot.totalArmsDealers = 4;
		snapshot.totalPalaces = 1;
		snapshot.completedBlackMarkets = 3;
		snapshot.effectiveTotalBlackMarkets = 3;
		snapshot.canScaleMilitaryProduction = true;
		snapshot.sprawlSupplyCap = 30;
		snapshot.effectiveBarracksCap = 6;
		snapshot.effectiveArmsCap = 6;
		snapshot.sprawlMarketCap = 12;
		snapshot.sprawlTunnelCap = 12;
		snapshot.sprawlStingerCap = 12;
		snapshot.blackMarketBuildCooldownReady = true;
		snapshot.barracksBuildCooldownReady = true;
		snapshot.armsDealerBuildCooldownReady = true;
		snapshot.tunnelBuildCooldownReady = true;
		snapshot.stingerBuildCooldownReady = true;
		snapshot.palaceBuildCooldownReady = true;
		return snapshot;
	}
}

int main()
{
	const AIControlAdapterMacroBuildManager manager;

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.developedZoneCount = 7;
		snapshot.supplyFootprintRadius = 900.0f;
		snapshot.remoteSupplyZoneCount = 1;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.choice.index >= 0, "Urgent expansion plan should select an intent");
		expect(plan.choice.category == std::string("expansion"), "Urgent expansion should outrank market growth");
		expect(plan.choice.command == std::string("Game.BuildSupplyStashSmart"), "Urgent expansion should select supply stash command");
		expect(plan.telemetry.developedZoneCount == 7, "Macro build telemetry should expose developed zone count");
		expect(plan.telemetry.zoneGap == 20, "Macro build telemetry should expose zone gap");
		expect(plan.telemetry.coverageExpansionUrgent, "Macro build telemetry should expose coverage urgency");
		expect(plan.telemetry.preferRemoteSupplyExpansion, "Macro build telemetry should expose remote expansion preference");
		expect(plan.telemetry.expansionMode == std::string("urgent"), "Macro build telemetry should expose expansion mode");
		expect(plan.telemetry.expansionReason == std::string("coverage_gap_high_cash"), "Macro build telemetry should expose expansion reason");
		expect(plan.telemetry.remoteSupplyStage == std::string("full_map"), "Mature sprawl should allow full-map remote expansion");
		expect(plan.telemetry.remoteSupplyAllowed, "Remote expansion should be allowed after local supply is established");
		expect(plan.telemetry.remoteSupplyMaxDistance == 0.0f, "Full-map remote expansion should not cap max distance");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.totalSupplyStashes = 1;
		snapshot.localSupplyEstablished = false;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.telemetry.preferRemoteSupplyExpansion, "Urgent coverage should still prefer remote expansion");
		expect(!plan.telemetry.remoteSupplyAllowed, "Local bootstrap should block remote supply arguments");
		expect(plan.telemetry.remoteSupplyStage == std::string("local_bootstrap"), "Bootstrap stage should be explicit");
		expect(plan.telemetry.remoteSupplyReason == std::string("local_bootstrap"), "Bootstrap blocker should be explicit");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.stashZoneCount = 3;
		snapshot.developedZoneCount = 1;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.telemetry.remoteSupplyAllowed, "Near ring should allow remote supply selection");
		expect(plan.telemetry.remoteSupplyStage == std::string("near_ring"), "Early staged expansion should use near ring");
		expect(plan.telemetry.remoteSupplyMaxDistance == 2200.0f, "Near ring should cap remote selection distance");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.stashZoneCount = 6;
		snapshot.developedZoneCount = 3;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.telemetry.remoteSupplyAllowed, "Mid ring should allow remote supply selection");
		expect(plan.telemetry.remoteSupplyStage == std::string("mid_ring"), "Mid staged expansion should use mid ring");
		expect(plan.telemetry.remoteSupplyMaxDistance == 3400.0f, "Mid ring should cap remote selection distance");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.expansionSpend.allowed = false;
		snapshot.expansionSpend.reason = "reserve_protected";
		snapshot.remoteZoneHasStash = true;
		snapshot.allowExpansionBeforeFullRemoteFollowup = true;
		snapshot.totalZoneTunnels = 1;
		snapshot.totalZoneStingers = 0;
		snapshot.zoneSeedCooldownReady = true;
		snapshot.activeZoneStingersInProgress = 0;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.choice.index >= 0, "Blocked expansion should still allow a valid zone seed intent");
		expect(plan.choice.category == std::string("zone_seed"), "Blocked expansion should fall through to zone seed");
		expect(plan.choice.command == std::string("Game.BuildStingerSite"), "Zone seed should select stinger command");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.stashZoneCount = 30;
		snapshot.money = 9000u;
		snapshot.totalSupplyStashes = 2;
		snapshot.effectiveTotalBlackMarkets = 2;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.choice.index >= 0, "Eco recovery should emit a valid recovery intent");
		expect(plan.choice.category == std::string("eco_recovery"), "Eco recovery should be selected when expansion is not active");
		expect(plan.choice.command == std::string("Game.BuildSupplyStashSmart"), "Supply recovery should select supply stash command");
		expect(plan.intents[static_cast<std::size_t>(plan.choice.index)].executor == AIControlAdapterMacroBuildExecutor::SupplyExpansion, "Supply recovery should use supply expansion executor");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.expansionSpend.allowed = false;
		snapshot.expansionSpend.reason = "reserve_protected";
		snapshot.stashZoneCount = 30;
		snapshot.desiredZoneCount = 30;
		snapshot.shouldPreserveReserve = true;
		snapshot.totalBarracks = 6;
		snapshot.totalArmsDealers = 6;
		snapshot.effectiveTotalBlackMarkets = 3;
		snapshot.marketGrowthSpend.allowed = false;
		snapshot.marketGrowthSpend.reason = "reserve_protected";
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.choice.index == -1, "Blocked intents should produce no selected macro build intent");
		expect(!plan.intents.empty(), "Blocked intents should remain visible for telemetry");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.stashZoneCount = 30;
		snapshot.desiredZoneCount = 30;
		snapshot.effectiveTotalBlackMarkets = 12;
		snapshot.totalBarracks = 4;
		snapshot.effectiveBarracksCap = 6;
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.choice.index >= 0, "Producer growth should emit a valid intent");
		expect(plan.choice.category == std::string("producer_growth"), "Producer growth should be manager-owned");
		expect(plan.choice.command == std::string("Game.BuildBarracksSmart"), "Producer growth should prefer barracks before arms dealer");
	}

	{
		AIControlAdapterMacroBuildSnapshot snapshot = makeBaseSnapshot();
		snapshot.stashZoneCount = 30;
		snapshot.desiredZoneCount = 30;
		snapshot.effectiveTotalBlackMarkets = 12;
		snapshot.totalBarracks = 6;
		snapshot.totalArmsDealers = 6;
		snapshot.staticDefenseZoneIndex = 2;
		snapshot.staticDefenseCommand = "Game.BuildStingerSite";
		snapshot.staticDefenseReason = "frontier_floor";
		const AIControlAdapterMacroBuildPlan plan = manager.BuildPlan(snapshot);
		expect(plan.choice.index >= 0, "Static defense should emit a valid specific-zone intent");
		expect(plan.choice.category == std::string("static_defense"), "Static defense should be manager-owned");
		expect(plan.choice.command == std::string("Game.BuildStingerSite"), "Static defense should preserve command");
		expect(plan.intents[static_cast<std::size_t>(plan.choice.index)].executor == AIControlAdapterMacroBuildExecutor::SpecificZone, "Static defense should use specific-zone executor");
		expect(plan.intents[static_cast<std::size_t>(plan.choice.index)].zoneIndex == 2, "Static defense should preserve zone index");
	}

	{
		AIControlAdapterOpeningBuildDecision decision = manager.EvaluateOpeningBuild({
			"Game.BuildSupplyStashSmart",
			false,
			1300u,
			0,
			0,
			0,
			0,
			0,
			true,
			true,
			true
		});
		expect(decision.handled, "Opening stash requirement should be handled");
		expect(decision.command == std::string("Game.BuildSupplyStashSmart"), "Ready opening stash should request supply build");
		expect(decision.executor == AIControlAdapterMacroBuildExecutor::SupplyExpansion, "Opening stash should use supply executor");
		expect(decision.allowBarracksFallbackAfterSupplyFailure, "Opening stash placement failure should allow barracks fallback");
	}

	{
		AIControlAdapterOpeningBuildDecision decision = manager.EvaluateOpeningBuild({
			"Game.BuildSupplyStashSmart",
			false,
			700u,
			0,
			1,
			0,
			0,
			0,
			true,
			true,
			true
		});
		expect(decision.handled, "Opening stash in progress should be handled");
		expect(decision.command == std::string("Game.BuildBarracksSmart"), "Opening should allow barracks while stash is in progress");
		expect(decision.displayCommand == std::string("Game.BuildBarracksSmart"), "Opening display command should switch to barracks fallback");
	}

	{
		AIControlAdapterOpeningBuildDecision decision = manager.EvaluateOpeningBuild({
			"Game.BuildBarracksSmart",
			false,
			500u,
			0,
			0,
			0,
			0,
			1,
			true,
			true,
			true
		});
		expect(decision.handled, "Opening barracks requirement should be handled");
		expect(decision.command == nullptr, "Low cash should not request barracks");
		expect(decision.reason == std::string("opening_wait_money"), "Low cash should expose wait money");
	}

	{
		AIControlAdapterOpeningBuildDecision decision = manager.EvaluateOpeningBuild({
			"Game.BuildArmsDealerSmart",
			false,
			5000u,
			1,
			0,
			0,
			1,
			1,
			true,
			true,
			true
		});
		expect(decision.handled, "Opening arms dealer requirement should be handled");
		expect(decision.command == nullptr, "Arms dealer in progress should not request another");
		expect(decision.reason == std::string("opening_wait_arms_dealer"), "Arms dealer in progress should expose wait reason");
	}

	{
		AIControlAdapterOpeningBuildDecision decision = manager.EvaluateOpeningBuild({
			nullptr,
			true,
			1700u,
			1,
			0,
			0,
			0,
			1,
			true,
			true,
			true
		});
		expect(decision.handled, "Second stash policy should block later macro branches");
		expect(decision.command == std::string("Game.BuildSupplyStashSmart"), "Ready second stash should request supply build");
		expect(decision.executor == AIControlAdapterMacroBuildExecutor::SupplyExpansion, "Second stash should use supply executor");
	}

	{
		AIControlAdapterOpeningBuildDecision decision = manager.EvaluateOpeningBuild({
			nullptr,
			true,
			1500u,
			1,
			0,
			0,
			0,
			1,
			true,
			true,
			true
		});
		expect(decision.handled, "Blocked second stash should still consume the macro branch");
		expect(decision.command == nullptr, "Blocked second stash should not request a command");
		expect(decision.reason == std::string(""), "Blocked second stash should preserve old empty reason behavior");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				true,
				false,
				4,
				30,
				0,
				0,
				false,
				true,
				4,
				1,
				12,
				false,
				2,
				4,
				true,
				false
			}) == std::string("macro_hold_reserve"),
			"Reserve preservation should be the first hold reason");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				true,
				true,
				4,
				30,
				1,
				0,
				false,
				true,
				4,
				1,
				12,
				false,
				2,
				4,
				true,
				false
			}) == std::string("macro_wait_expansion_in_progress"),
			"Urgent expansion should report supply in-progress before other blockers");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				true,
				4,
				30,
				0,
				0,
				true,
				true,
				4,
				1,
				12,
				false,
				2,
				4,
				true,
				false
			}) == std::string("macro_wait_expansion_throttle"),
			"Urgent expansion should report throttle before economy fallback");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				true,
				4,
				30,
				0,
				0,
				false,
				false,
				0,
				1,
				12,
				false,
				2,
				4,
				true,
				false
			}) == std::string("macro_wait_non_supply_anchor_economy"),
			"Urgent expansion should expose non-supply economy blocker");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				false,
				30,
				30,
				0,
				0,
				false,
				true,
				4,
				1,
				12,
				true,
				2,
				4,
				true,
				false
			}) == std::string("macro_wait_eco_recovery"),
			"Eco recovery should explain non-issued macro tick before market cash");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				false,
				30,
				30,
				0,
				0,
				false,
				true,
				4,
				1,
				12,
				false,
				2,
				4,
				false,
				true
			}) == std::string("macro_wait_market_cash"),
			"Market cash should outrank remote zone follow-up in hold reason order");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				false,
				30,
				30,
				0,
				0,
				false,
				true,
				4,
				1,
				12,
				false,
				4,
				4,
				true,
				true
			}) == std::string("macro_wait_zone_followup"),
			"Remote follow-up should be exposed when market cash is not blocked");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				false,
				20,
				30,
				0,
				0,
				false,
				true,
				4,
				1,
				12,
				false,
				4,
				4,
				true,
				false
			}) == std::string("macro_wait_zone_expansion"),
			"Below target zones should report normal expansion wait");
	}

	{
		expect(
			manager.EvaluateMacroHoldReason({
				false,
				false,
				30,
				30,
				0,
				0,
				false,
				true,
				4,
				1,
				12,
				false,
				4,
				4,
				true,
				false
			}) == std::string("macro_no_priority"),
			"Satisfied macro state should report no priority");
	}

	{
		AIControlAdapterStaticDefensePolicyResult policy{};
		policy.shouldBuildStinger = true;
		policy.shouldBuildTunnel = true;
		policy.reason = "below_desired";
		const AIControlAdapterStaticDefenseCandidateDecision decision =
			manager.EvaluateStaticDefenseCandidate({
				&policy,
				false,
				false,
				true,
				50000u,
				true,
				true
			});
		expect(decision.shouldBuild, "Static defense candidate should be allowed when gates pass");
		expect(decision.command == std::string("Game.BuildStingerSite"), "Static defense should prefer stinger when both are needed");
		expect(decision.reason == std::string("below_desired"), "Static defense candidate should preserve policy reason");
	}

	{
		AIControlAdapterStaticDefensePolicyResult policy{};
		policy.shouldBuildTunnel = true;
		policy.reason = "below_desired";
		const AIControlAdapterStaticDefenseCandidateDecision decision =
			manager.EvaluateStaticDefenseCandidate({
				&policy,
				false,
				false,
				false,
				1000u,
				false,
				true
			});
		expect(decision.shouldBuild, "Static defense should allow tunnel fallback");
		expect(decision.command == std::string("Game.BuildTunnelNetwork"), "Static defense should select tunnel when stinger is not requested");
	}

	{
		AIControlAdapterStaticDefensePolicyResult policy{};
		policy.shouldBuildStinger = true;
		policy.reason = "below_desired";
		const AIControlAdapterStaticDefenseCandidateDecision decision =
			manager.EvaluateStaticDefenseCandidate({
				&policy,
				true,
				false,
				true,
				50000u,
				true,
				true
			});
		expect(!decision.shouldBuild, "Urgent expansion should block static defense candidate");
		expect(decision.reason == std::string("urgent_expansion_priority"), "Static defense candidate should expose urgent expansion blocker");
	}

	{
		AIControlAdapterStaticDefensePolicyResult policy{};
		policy.shouldBuildStinger = true;
		policy.reason = "below_desired";
		const AIControlAdapterStaticDefenseCandidateDecision decision =
			manager.EvaluateStaticDefenseCandidate({
				&policy,
				false,
				true,
				true,
				50000u,
				true,
				true
			});
		expect(!decision.shouldBuild, "Reserve protection should block static defense candidate");
		expect(decision.reason == std::string("reserve_protected"), "Static defense candidate should expose reserve blocker");
	}

	{
		AIControlAdapterPalaceRedundancyResult policy{};
		policy.shouldBuild = true;
		policy.reason = "anchor_redundancy";
		const AIControlAdapterPalaceRedundancyCandidateDecision decision =
			manager.EvaluatePalaceRedundancyCandidate({
				&policy,
				true
			});
		expect(decision.shouldBuild, "Palace redundancy candidate should be allowed when build is ready");
		expect(decision.reason == std::string("anchor_redundancy"), "Palace redundancy candidate should preserve policy reason");
	}

	{
		AIControlAdapterPalaceRedundancyResult policy{};
		policy.shouldBuild = true;
		policy.reason = "anchor_redundancy";
		const AIControlAdapterPalaceRedundancyCandidateDecision decision =
			manager.EvaluatePalaceRedundancyCandidate({
				&policy,
				false
			});
		expect(!decision.shouldBuild, "Palace cooldown should block redundancy candidate");
		expect(decision.reason == std::string("cooldown"), "Palace redundancy candidate should expose cooldown blocker");
	}

	{
		AIControlAdapterPalaceRecoveryDecision policy{};
		policy.shouldBuild = true;
		policy.reason = "enemy_wmd_detected";
		const AIControlAdapterPalaceRecoveryCandidateDecision decision =
			manager.EvaluatePalaceRecoveryCandidate({
				&policy,
				true
			});
		expect(decision.shouldAttempt, "Palace recovery should attempt when policy and opening infrastructure allow it");
		expect(decision.command == std::string("Game.BuildPalaceSmart"), "Palace recovery should request Palace command");
		expect(decision.reason == std::string("enemy_wmd_detected"), "Palace recovery candidate should preserve policy reason");
	}

	{
		AIControlAdapterPalaceRecoveryDecision policy{};
		policy.shouldBuild = true;
		policy.reason = "enemy_wmd_detected";
		const AIControlAdapterPalaceRecoveryCandidateDecision decision =
			manager.EvaluatePalaceRecoveryCandidate({
				&policy,
				false
			});
		expect(!decision.shouldAttempt, "Missing opening infrastructure should block Palace recovery attempt");
		expect(decision.reason == std::string("placement_unavailable"), "Missing opening infrastructure should normalize to placement unavailable");
	}

	{
		AIControlAdapterPalaceRecoveryDecision policy{};
		policy.shouldBuild = true;
		policy.reason = "enemy_wmd_detected";
		const AIControlAdapterPalaceRecoveryResultDecision result =
			manager.EvaluatePalaceRecoveryResult({
				&policy,
				true,
				true,
				false
			});
		expect(!result.issued, "Failed Palace recovery command should not be marked issued");
		expect(result.reason == std::string("placement_unavailable"), "Failed Palace recovery command should log placement unavailable");
	}

	{
		AIControlAdapterPalaceRecoveryDecision policy{};
		policy.shouldBuild = false;
		policy.reason = "target_met";
		const AIControlAdapterPalaceRecoveryResultDecision result =
			manager.EvaluatePalaceRecoveryResult({
				&policy,
				true,
				false,
				false
			});
		expect(!result.issued, "Non-needed Palace recovery should not be marked issued");
		expect(result.reason == std::string("target_met"), "Non-needed Palace recovery should preserve policy reason");
	}

	{
		AIControlAdapterNonSupplyFootholdDecision decision = manager.EvaluateNonSupplyFoothold({
			false,
			true,
			8,
			30,
			true,
			"placement_failed",
			false,
			0,
			50000u,
			0,
			12,
			0,
			true
		});
		expect(!decision.shouldEvaluatePlacement, "Foothold should wait for strong economy");
		expect(decision.reason == std::string("economy_not_ready"), "Foothold should expose economy blocker");
	}

	{
		AIControlAdapterNonSupplyFootholdDecision decision = manager.EvaluateNonSupplyFoothold({
			false,
			true,
			8,
			30,
			true,
			"macro_wait_expansion_placement",
			true,
			4,
			50000u,
			0,
			12,
			0,
			true
		});
		expect(decision.shouldEvaluatePlacement, "High durable income should allow foothold placement evaluation");
		expect(decision.shouldAttemptTunnel, "Available tunnel should allow foothold tunnel attempt");
		expect(decision.anchorType == ZoneAnchorType::MarketFoothold, "Strong income should select market foothold");
		expect(decision.reason == std::string("non_supply_expansion"), "Allowed foothold should expose action reason");
	}

	{
		AIControlAdapterNonSupplyFootholdDecision decision = manager.EvaluateNonSupplyFoothold({
			false,
			true,
			8,
			30,
			true,
			"no_worker",
			true,
			4,
			50000u,
			12,
			12,
			0,
			true
		});
		expect(decision.shouldEvaluatePlacement, "Blocked supply expansion should allow foothold placement evaluation");
		expect(!decision.shouldAttemptTunnel, "Tunnel cap should block foothold tunnel attempt");
		expect(decision.reason == std::string("tunnel_cap_reached"), "Foothold should expose tunnel cap blocker");
	}

	std::cout << "AIControlAdapterMacroBuildManagerTests passed\n";
	return 0;
}
