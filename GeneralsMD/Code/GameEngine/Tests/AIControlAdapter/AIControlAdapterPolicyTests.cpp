#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameClient/AIControlAdapter/AIControlAdapterEnemyMemory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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

	void expectNear(float actual, float expected, float epsilon, const char* message)
	{
		if (std::fabs(actual - expected) > epsilon)
		{
			std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
			std::exit(1);
		}
	}

	AIControlAdapterScudStormStrategicTargetCandidate makeScudStormTarget(
		unsigned int objectId,
		const char* kind,
		const char* templateName,
		bool visible,
		unsigned int ageMs)
	{
		AIControlAdapterScudStormStrategicTargetCandidate candidate;
		candidate.objectId = objectId;
		candidate.playerIndex = 2;
		candidate.team = 1;
		candidate.targetKind = kind;
		candidate.templateName = templateName;
		candidate.visible = visible;
		candidate.stale = !visible;
		candidate.enemyOwned = true;
		candidate.alive = true;
		candidate.ageMs = ageMs;
		candidate.x = 1000.0f + static_cast<float>(objectId);
		candidate.y = 2000.0f + static_cast<float>(objectId);
		candidate.z = 0.0f;
		return candidate;
	}
}

int main()
{
	{
		const AIControlAdapterProfilePolicyConfig config = AIControlAdapterResolveProfilePolicyConfig(
			"sprawl_balanced",
			0.65f,
			0.35f,
			0.45f,
			0.55f,
			10.0f);
		expect(config.isBalancedSprawl, "profile config should identify balanced sprawl");
		expect(config.isSprawlStyle, "balanced sprawl should be sprawl style");
		expect(config.reserveCash == 10000u, "balanced sprawl should resolve 10000 reserve");
		expect(config.workerMinIdle == 2, "balanced sprawl should keep two idle workers");
		expect(config.workerQueueCount == 1, "balanced sprawl should queue one worker per pulse");
		expect(config.stashWorkersPerStash == 6, "balanced sprawl should keep tuned stash worker target");
		expect(config.attackMinUnits == 55, "balanced sprawl should keep current attack threshold");
		expect(config.attackGroupSize == 28, "balanced sprawl should keep current attack group size");
		expect(config.sprawlSupplyCap == 30, "balanced sprawl multiplier 10 should resolve 30 supply zones");
		expect(config.sprawlBarracksCap == 15, "balanced sprawl multiplier 10 should resolve 15 barracks");
		expect(config.sprawlArmsCap == 20, "balanced sprawl multiplier 10 should resolve 20 arms dealers");
		expect(config.sprawlMarketCap == 60, "balanced sprawl multiplier 10 should resolve 60 markets");
		expect(config.sprawlTunnelCap == 50, "balanced sprawl multiplier 10 should resolve 50 tunnels");
		expect(config.sprawlStingerCap == 40, "balanced sprawl multiplier 10 should resolve 40 stingers");
		expect(config.urgentZoneGapThreshold == 5, "profile config should expose urgent expansion gap threshold");
		expect(config.normalMaxConcurrentExpansionStashes == 3, "maxed balanced sprawl should keep three normal concurrent expansion stashes");
		expect(config.allowExpansionBeforeFullRemoteFollowup, "maxed balanced sprawl should allow seeded zones before full follow-up");
	}

	{
		const AIControlAdapterProfilePolicyConfig config = AIControlAdapterResolveProfilePolicyConfig(
			"sprawl",
			0.65f,
			0.35f,
			0.45f,
			0.55f,
			10.0f);
		expect(!config.isBalancedSprawl, "plain sprawl should not be balanced sprawl");
		expect(config.isSprawlStyle, "plain sprawl should be sprawl style");
		expect(config.reserveCash == 5000u, "plain sprawl should resolve 5000 reserve");
		expect(config.workerMinIdle == 3, "plain sprawl should keep current worker idle target");
		expect(config.stashWorkersPerStash == 3, "plain sprawl should keep current stash worker target");
		expect(config.attackMinUnits == 70, "plain sprawl should keep current attack threshold");
		expect(config.sprawlSupplyCap == 40, "plain sprawl multiplier 10 should resolve 40 supply zones");
		expect(config.sprawlTunnelCap == 80, "plain sprawl multiplier 10 should resolve 80 tunnels");
		expect(config.normalMaxConcurrentExpansionStashes == 4, "maxed plain sprawl should allow four concurrent expansion stashes");
		expect(config.allowExpansionBeforeFullRemoteFollowup, "maxed plain sprawl should allow seeded zones before full follow-up");
	}

	{
		const AIControlAdapterProfilePolicyConfig config = AIControlAdapterResolveProfilePolicyConfig(
			"aggressive",
			0.5f,
			0.5f,
			0.5f,
			0.5f,
			20.0f);
		expect(!config.isSprawlStyle, "aggressive should not be sprawl style");
		expect(config.reserveCash == 0u, "non-sprawl profiles should resolve no sprawl reserve");
		expect(config.workerCooldownMs == 1500u, "aggressive should keep fast worker cooldown");
		expect(config.attackMinUnits == 24, "aggressive profile should keep current attack threshold");
		expect(config.sprawlSupplyCap == 40, "sprawl multiplier should be clamped to 10");
		expect(config.normalMaxConcurrentExpansionStashes == 1, "non-sprawl profiles should keep one concurrent expansion stash");
		expect(!config.allowExpansionBeforeFullRemoteFollowup, "non-sprawl profiles should require normal follow-up semantics");
	}

	{
		const AIControlAdapterRemoteZoneFollowupResult result = AIControlAdapterChooseRemoteZoneFollowup({
			true,  // remoteZoneHasStash
			1,     // tunnels
			0,     // barracks
			0,     // armsDealers
			1,     // stingers
			true   // allowExpansionBeforeFullRemoteFollowup
		});
		expect(!result.needsFollowup, "max-sprawl seed policy should allow expansion after tunnel and stinger are present");
		expect(std::string(result.packageStage) == "seeded", "seeded zone should expose seeded stage");
		expect(std::string(result.reason) == "seeded_defense_sufficient", "seeded zone should expose concrete reason");
	}

	{
		const AIControlAdapterRemoteZoneFollowupResult result = AIControlAdapterChooseRemoteZoneFollowup({
			true,  // remoteZoneHasStash
			1,     // tunnels
			0,     // barracks
			0,     // armsDealers
			1,     // stingers
			false  // allowExpansionBeforeFullRemoteFollowup
		});
		expect(result.needsFollowup, "normal policy should still require production follow-up after defenses");
		expect(std::string(result.packageStage) == "barracks", "normal follow-up should request barracks next");
		expect(std::string(result.reason) == "needs_barracks", "normal follow-up should expose barracks reason");
	}

	{
		const AIControlAdapterRemoteZoneFollowupResult result = AIControlAdapterChooseRemoteZoneFollowup({
			true,  // remoteZoneHasStash
			0,     // tunnels
			0,     // barracks
			0,     // armsDealers
			0,     // stingers
			true   // allowExpansionBeforeFullRemoteFollowup
		});
		expect(result.needsFollowup, "seed policy should still require first defensive seed");
		expect(std::string(result.packageStage) == "tunnel", "first seed package stage should be tunnel");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			false,
			false,
			false,
			false,
			0u,
			10000u,
			0,
			0
		};
		expect(!AIControlAdapterShouldPauseCombatProduction(inputs), "non-balanced profiles should not pause via balanced reserve policy");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			false,
			false,
			false,
			50000u,
			10000u,
			0,
			0
		};
		expect(AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should pause until opening infrastructure is ready");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			false,
			false,
			50000u,
			10000u,
			0,
			0
		};
		expect(AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should pause until opening economy is ready");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			false,
			9000u,
			10000u,
			0,
			0
		};
		expect(AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should pause below reserve cash");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			false,
			11000u,
			10000u,
			1,
			0
		};
		expect(AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should pause near reserve while eco structures are in progress");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			false,
			12000u,
			10000u,
			0,
			0
		};
		expect(!AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should resume at reserve threshold when no eco structures are in progress");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			false,
			12000u,
			10000u,
			1,
			0
		};
		expect(!AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should resume at reserve plus eco buffer");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			true,
			10050u,
			10000u,
			0,
			0
		};
		expect(AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should keep recovering briefly after crossing reserve cash");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			true,
			12999u,
			10000u,
			0,
			0
		};
		expect(AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should keep recovering until it clears the widened reserve hysteresis buffer");
	}

	{
		const AIControlAdapterProductionPolicyInputs inputs = {
			true,
			true,
			true,
			true,
			13000u,
			10000u,
			0,
			0
		};
		expect(!AIControlAdapterShouldPauseCombatProduction(inputs), "balanced sprawl should resume after clearing the widened reserve hysteresis buffer");
	}

	expect(!AIControlAdapterShouldHoldArmyCap({
		true,
		true,
		false,
		100,
		100
	}), "army cap should not be evaluated while economy pause is active");
	expect(AIControlAdapterShouldHoldArmyCap({
		false,
		true,
		false,
		100,
		100
	}), "balanced sprawl should hold at the hard army cap");
	expect(AIControlAdapterShouldHoldArmyCap({
		false,
		true,
		true,
		95,
		100
	}), "balanced sprawl should hold production until the army drops below the hysteresis threshold");
	expect(!AIControlAdapterShouldHoldArmyCap({
		false,
		true,
		true,
		89,
		100
	}), "balanced sprawl should resume production after dropping clearly below the hysteresis threshold");
	expect(!AIControlAdapterShouldHoldArmyCap({
		false,
		false,
		false,
		100,
		100
	}), "non-balanced profiles should not use the balanced army-cap hold rule");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		99999u,
		100000,
		20,
		42,
		0,
		100
	}) == 100, "balanced sprawl should keep the normal army cap below surplus-cash pressure");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		100000u,
		93000,
		20,
		42,
		0,
		100
	}) == 286, "balanced sprawl should scale army cap with production capacity under surplus-cash pressure");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		100000u,
		10000,
		20,
		42,
		0,
		100
	}) == 120, "balanced sprawl should limit surplus army cap when net income is modest");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		100000u,
		-1000,
		20,
		42,
		0,
		100
	}) == 100, "balanced sprawl should not raise army cap while net cash flow is negative");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		500000u,
		200000,
		100,
		100,
		0,
		100
	}) == 300, "balanced sprawl surplus army cap should stay bounded");
	expect(AIControlAdapterGetEffectiveArmyCap({
		false,
		500000u,
		0,
		20,
		42,
		0,
		9999
	}) == 9999, "non-balanced profiles should keep their configured army cap");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		300000u,
		-1000,
		17,
		22,
		27,
		100
	}) == 217, "mature balanced sprawl economy should not collapse to base cap on a negative income sample");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		300000u,
		-1000,
		2,
		8,
		27,
		100
	}) == 130, "late-game cash floor should still be bounded by producer capacity");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		500000u,
		-1000,
		100,
		100,
		27,
		100
	}) == 300, "late-game cash floor should still be bounded by hard cap");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		300000u,
		-1000,
		20,
		42,
		0,
		100
	}) == 100, "large cash without completed markets should keep conservative negative-income cap");

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			false,
			true,
			30000u,
			10000u,
			0,
			0
		};
		expect(!AIControlAdapterCanAttemptBlackMarket(inputs), "black market should require a finished palace");
	}

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			true,
			true,
			11000u,
			10000u,
			1,
			0
		};
		expect(!AIControlAdapterCanAttemptBlackMarket(inputs), "balanced sprawl should not start additional black markets below reserve plus cost");
	}

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			true,
			true,
			10000u,
			10000u,
			0,
			0
		};
		expect(AIControlAdapterCanAttemptBlackMarket(inputs), "balanced sprawl should allow the first black market at reserve");
	}

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			true,
			true,
			12500u,
			10000u,
			1,
			0
		};
		expect(AIControlAdapterCanAttemptBlackMarket(inputs), "balanced sprawl should allow additional black markets at reserve plus cost");
	}

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			true,
			true,
			20000u,
			10000u,
			1,
			1
		};
		expect(!AIControlAdapterCanAttemptBlackMarket(inputs), "balanced sprawl should not start another black market while one is in progress");
	}

	expect(std::string(AIControlAdapterGetRequiredOpeningBuild({ 0, 0, 0 })) == "Game.BuildSupplyStashSmart", "opening should require stash first");
	expect(std::string(AIControlAdapterGetRequiredOpeningBuild({ 1, 0, 0 })) == "Game.BuildBarracksSmart", "opening should require barracks after stash");
	expect(std::string(AIControlAdapterGetRequiredOpeningBuild({ 1, 1, 0 })) == "Game.BuildArmsDealerSmart", "opening should require arms dealer after barracks");
	expect(AIControlAdapterGetRequiredOpeningBuild({ 1, 1, 1 }) == nullptr, "opening should be complete after stash, barracks, and arms dealer");

	expect(std::string(AIControlAdapterGetEcoRecoveryBuild({
		true,
		1,
		0,
		0,
		2,
		false,
		false
	})) == "Game.BuildSupplyStashSmart", "eco recovery should prefer a second stash before other recovery options");
	expect(std::string(AIControlAdapterGetEcoRecoveryBuild({
		true,
		2,
		1,
		0,
		2,
		false,
		true
	})) == "Game.BuildSupplyStashSmart", "eco recovery should still prefer early supply expansion before markets");
	expect(std::string(AIControlAdapterGetEcoRecoveryBuild({
		true,
		3,
		1,
		0,
		2,
		false,
		true
	})) == "Game.BuildBlackMarketSmart", "eco recovery should prefer markets once basic supply expansion exists");
	expect(std::string(AIControlAdapterGetEcoRecoveryBuild({
		true,
		3,
		0,
		0,
		2,
		false,
		false
	})) == "Game.BuildSupplyStashSmart", "eco recovery should fall back to stash growth when market recovery is unavailable");
	expect(AIControlAdapterGetEcoRecoveryBuild({
		true,
		3,
		1,
		2,
		2,
		true,
		false
	}) == nullptr, "eco recovery should return none when stash growth is throttled and market goals are already met");

	expect(AIControlAdapterIsSettlingSensitiveBuild("Game.BuildSupplyStashSmart"), "supply stash should be treated as settling-sensitive");
	expect(AIControlAdapterIsSettlingSensitiveBuild("Game.BuildTunnelNetwork"), "tunnel network should be treated as settling-sensitive");
	expect(AIControlAdapterIsSettlingSensitiveBuild("Game.BuildStingerSite"), "stinger site should be treated as settling-sensitive");
	expect(!AIControlAdapterIsSettlingSensitiveBuild("Game.BuildBlackMarketSmart"), "black market should not be treated as settling-sensitive");
	expect(AIControlAdapterGetBuildRetryDelayMs("Game.BuildSupplyStashSmart", false, "construct_site_not_created") == 2500u, "stash retries should be aggressive after construct site failures");
	expect(AIControlAdapterGetBuildRetryDelayMs("Game.BuildBarracksSmart", false, "construct_site_not_created") == 3000u, "barracks retries should be faster after construct site failures");
	expect(AIControlAdapterGetBuildRetryDelayMs("Game.BuildArmsDealerSmart", false, "construct_site_not_created") == 3500u, "arms dealer retries should be faster after construct site failures");
	expect(AIControlAdapterGetBuildRetryDelayMs("Game.BuildTunnelNetwork", false, "construct_site_not_created") == 4500u, "tunnel retries should respect settling-sensitive fallback timing");
	expect(AIControlAdapterGetBuildRetryDelayMs("Game.BuildStingerSite", false, "idle_worker_not_found") == 2000u, "stinger retries should recover quickly from idle worker shortages");
	expect(AIControlAdapterGetBuildRetryDelayMs("Game.BuildPalaceSmart", true, "") == 12000u, "palace success should retain a longer cooldown");

	expect(AIControlAdapterShouldAbortSciencePlanForTick("science_not_purchasable"), "science_not_purchasable should stop science attempts for the current tick");
	expect(!AIControlAdapterShouldAbortSciencePlanForTick("no_money"), "other science errors should not necessarily stop the science plan");
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("queue_full"), "queue_full should stop upgrade attempts for the current tick");
	expect(!AIControlAdapterShouldAbortUpgradePlanForTick("producer_cannot_make_upgrade"), "producer_cannot_make_upgrade should skip the current upgrade and allow later upgrades in the plan");
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("palace_not_found"), "palace_not_found should stop upgrade attempts for the current tick");
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("black_market_not_found"), "black_market_not_found should stop upgrade attempts for the current tick");
	expect(!AIControlAdapterShouldAbortUpgradePlanForTick("upgrade_already_in_production"), "upgrade_already_in_production should skip the current upgrade and allow later upgrades in the plan");
	expect(!AIControlAdapterShouldAbortUpgradePlanForTick("upgrade_already_complete"), "upgrade_already_complete should still allow later upgrades in the plan");
	expect(AIControlAdapterShouldAbortUpgradePlanForReason("upgrade_already_complete"), "upgrade_already_complete should abort the current upgrade scan to avoid repeated redundant requests");
	expect(AIControlAdapterGetTechRetryDelayMs(true, "ok") == 6000u, "successful tech actions should keep the short cadence");
	expect(AIControlAdapterGetTechRetryDelayMs(false, "tech_prereq_missing") == 12000u, "missing tech prerequisites should back off moderately");
	expect(AIControlAdapterGetTechRetryDelayMs(false, "science_not_purchasable") == 15000u, "non-actionable science should back off longer");
	expect(AIControlAdapterGetTechRetryDelayMs(false, "queue_full") == 15000u, "queue contention should back off longer");
	expect(AIControlAdapterGetProductionRetryDelayMs(true, "ok") == 2200u, "successful production should keep the normal cadence");
	expect(AIControlAdapterGetProductionRetryDelayMs(false, "queue_full") == 3500u, "queue-full production retries should back off instead of spamming every tick");
	expect(AIControlAdapterGetProductionRetryDelayMs(false, "producer_under_construction") == 5000u, "producer-under-construction retries should back off longer");
	expect(AIControlAdapterGetProductionRetryDelayMs(false, "no_prereq") == 6000u, "no-prereq production retries should back off the most");
	expect(AIControlAdapterGetProductionRetryDelayMs(false, "no_money") == 2500u, "no-money production retries should keep a moderate backoff");
	expect(AIControlAdapterGetProductionRetryDelayMs(false, "") == 1500u, "generic production failures should keep the default short retry");

	{
		const AIControlAdapterRadarVanPolicyInputs inputs = {
			true,
			false,
			0,
			0,
			5,
			1
		};
		expect(!AIControlAdapterShouldQueueRadarVan(inputs), "radar van automation should remain paused during reserve recovery");
	}

	{
		const AIControlAdapterRadarVanPolicyInputs inputs = {
			false,
			false,
			1,
			0,
			0,
			1
		};
		expect(!AIControlAdapterShouldQueueRadarVan(inputs), "radar van automation should not fire before any combat vehicles exist");
	}

	{
		const AIControlAdapterRadarVanPolicyInputs inputs = {
			false,
			false,
			1,
			0,
			1,
			1
		};
		expect(AIControlAdapterShouldQueueRadarVan(inputs), "radar van automation should allow the first radar van once combat vehicles exist");
	}

	{
		const AIControlAdapterRadarVanPolicyInputs inputs = {
			false,
			false,
			1,
			1,
			1,
			2
		};
		expect(!AIControlAdapterShouldQueueRadarVan(inputs), "radar vans should not be allowed to equal or exceed combat vehicle count");
	}

	{
		const AIControlAdapterRadarVanPolicyInputs inputs = {
			false,
			false,
			2,
			1,
			3,
			2
		};
		expect(AIControlAdapterShouldQueueRadarVan(inputs), "radar van automation should still allow another radar van when combat vehicles clearly outnumber them");
	}

	{
		const AIControlAdapterRadarVanPolicyInputs inputs = {
			false,
			true,
			2,
			0,
			6,
			1
		};
		expect(!AIControlAdapterShouldQueueRadarVan(inputs), "radar van automation should not add more units while the army cap hold is active");
	}

	{
		const AIControlAdapterVehicleSustainPolicyInputs inputs = {
			true,
			1,
			1,
			0,
			0,
			0,
			0,
			6,
			0
		};
		expect(AIControlAdapterShouldPreferVehicleReplenishment(inputs), "balanced sprawl should insist on vehicle replenishment when no frontline vehicles remain");
	}

	{
		const AIControlAdapterVehicleSustainPolicyInputs inputs = {
			true,
			2,
			2,
			2,
			0,
			0,
			0,
			14,
			6
		};
		expect(AIControlAdapterShouldPreferVehicleReplenishment(inputs), "balanced sprawl should replenish vehicles when infantry mass greatly exceeds the current vehicle floor");
	}

	{
		const AIControlAdapterVehicleSustainPolicyInputs inputs = {
			true,
			2,
			2,
			8,
			2,
			0,
			0,
			10,
			6
		};
		expect(!AIControlAdapterShouldPreferVehicleReplenishment(inputs), "balanced sprawl should stop forcing vehicles once the frontline vehicle floor is healthy");
	}

	{
		const AIControlAdapterMarketGrowthPolicyInputs inputs = {
			true,
			true,
			false,
			true,
			true,
			true,
			true,
			0,
			2,
			1,
			4,
			1,
			4
		};
		expect(AIControlAdapterShouldPrioritizeMarketsOverProductionBuildings(inputs), "forced eco recovery should prioritize markets over more production buildings");
	}

	{
		const AIControlAdapterMarketGrowthPolicyInputs inputs = {
			true,
			false,
			true,
			true,
			false,
			true,
			true,
			1,
			3,
			1,
			4,
			1,
			4
		};
		expect(AIControlAdapterShouldPrioritizeMarketsOverProductionBuildings(inputs), "balanced sprawl should continue prioritizing markets while below the desired market count");
	}

	{
		const AIControlAdapterMarketGrowthPolicyInputs inputs = {
			true,
			false,
			false,
			true,
			false,
			true,
			true,
			3,
			3,
			1,
			4,
			1,
			4
		};
		expect(!AIControlAdapterShouldPrioritizeMarketsOverProductionBuildings(inputs), "market priority should clear once the current market target is met");
	}

	{
		const AIControlAdapterMacroCompletionPolicyInputs inputs = {
			true,
			false,
			false,
			10,
			10,
			10,
			10,
			10,
			10,
			20,
			20,
			28,
			28,
			23,
			23
		};
		expect(AIControlAdapterShouldTreatMacroAsComplete(inputs), "sprawl macro should be considered complete once all structure caps are satisfied and no follow-up is pending");
	}

	{
		const AIControlAdapterMacroCompletionPolicyInputs inputs = {
			true,
			false,
			true,
			10,
			10,
			10,
			10,
			10,
			10,
			20,
			20,
			28,
			28,
			23,
			23
		};
		expect(!AIControlAdapterShouldTreatMacroAsComplete(inputs), "sprawl macro should stay active when a remote zone still needs follow-up");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				true,
				"reserve_cash_recovery",
				false,
			true,
			"sprawl_balanced",
			9000u,
			1,
			1,
				0,
				false,
				false,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				4,
				0,
				0,
			0,
			0,
			0,
			4,
			100
		});
		expect(result.command == nullptr, "production choice should not queue units during reserve recovery");
		expect(std::string(result.reason) == "reserve_cash_recovery", "production choice should preserve the pause reason");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			1,
			1,
				0,
				false,
				false,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				6,
				0,
				0,
			0,
			0,
			0,
			6,
			100
		});
		expect(std::string(result.command) == "Game.QueueQuadsAllWarFactories", "balanced sprawl should seed vehicles once arms dealers exist and no vehicles are present");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			1,
			1,
				0,
				false,
				false,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				12,
				8,
				0,
			0,
			0,
			0,
			20,
			100
		});
		expect(std::string(result.command) == "Game.QueueQuadsAllWarFactories", "balanced sprawl should keep replenishing vehicles when the frontline vehicle floor is still weak");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			1,
			1,
				0,
				false,
				false,
				false,
				0,
				0,  // captureSourcesLive
				0,  // captureSourcesReserved
				0,  // captureSourcesAvailable
				0,  // capturableTargetsRemaining
				0,  // desiredCaptureSources
				0,  // maxCaptureConcurrent
				6,  // soldiers
				0,  // rpg
				4,  // quads
			0,  // scorpions
			0,  // scudLaunchers
			0,  // radarVans
			10, // armyCount
			100 // armyCap
		});
		expect(std::string(result.command) == "Game.QueueScorpionsAllWarFactories", "balanced sprawl should keep building toward the vehicle target instead of immediately falling back to barracks-first spam");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			false,
			"standard",
			2000u,
			0,
			1,
				0,
				false,
				false,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				0,
				0,
				0,
			0,
			0,
			0,
			0,
			9999
	});
	expect(std::string(result.command) == "Game.QueueQuadsAllWarFactories", "non-barracks profiles should still choose vehicle production when only arms dealers are available");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				true,
			true,
			"sprawl_balanced",
			2000u,
			2,
			2,
				1,
				true,
				true,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				20,
				20,
				10,
			6,
			0,
			0,
			56,
			100
		});
		expect(result.command == nullptr, "balanced sprawl should hold production entirely while the army cap lock is active");
		expect(std::string(result.reason) == "army_cap_reached", "army cap hold should return a stable reason for logging and throttling");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			2,
			2,
				1,
				true,
				true,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				28,
				10,
				24,
			20,
			2,
			4,
			96,
			100
		});
		expect(result.command == nullptr, "balanced sprawl should stop queueing when the mobile army is near cap and both composition buckets are already healthy");
		expect(std::string(result.reason) == "army_cap_buffer", "balanced sprawl should report a stable near-cap buffer reason before hard cap hold engages");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			2,
			2,
				1,
				true,
				true,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				12,
				10,
				18,
			14,
			0,
			3,
			57,
			100
		});
			expect(std::string(result.command) == "Game.QueueRpgTroopersAllBarracks", "balanced sprawl should refill infantry when vehicles are already ahead and infantry is the larger end-state deficit");
		}

		{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
				true,
				"sprawl_balanced",
				2000u,
				2,
				2,
				1,
				true,
				true,
				true,
				0,
				0,     // captureSourcesLive
				0,     // captureSourcesReserved
				0,     // captureSourcesAvailable
				5,     // capturableTargetsRemaining
				3,     // desiredCaptureSources
				2,     // maxCaptureConcurrent
				12,
				10,
				18,
				14,
				0,
				3,
				57,
				100
			});
			expect(std::string(result.command) == "Game.QueueSoldiersAllBarracks", "balanced sprawl should force Rebel-capable infantry when capture upgrade is ready and capture sources are needed");
		}

		{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			2,
			2,
				1,
				false,
				true,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				12,
				10,
				4,
			0,
			0,
			3,
			26,
			100
		});
		expect(std::string(result.command) == "Game.QueueScorpionsAllWarFactories", "balanced sprawl should not choose Scud Launchers before the Palace prerequisite is actually completed");
	}

	{
			const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
				false,
				"",
				false,
			true,
			"sprawl_balanced",
			2000u,
			2,
			2,
				1,
				true,
				false,
				false,
				0,
				0, 0, 0, 0, 0, 0,  // Phase 6.3 fields
				12,
				10,
				4,
			0,
			0,
			3,
			26,
			100
		});
		expect(std::string(result.command) == "Game.QueueScorpionsAllWarFactories", "balanced sprawl should not choose Scud Launchers before SCIENCE_ScudLauncher is owned");
	}

	expect(AIControlAdapterHasTickElapsed(0u, 0u), "zero deadline should be ready immediately at tick zero");
	expect(AIControlAdapterHasTickElapsed(0u, 2155338000u), "zero deadline should remain ready even after long uptimes");
	expect(!AIControlAdapterIsTickInFuture(0u, 2155338000u), "zero deadline should never be treated as future work");
	expect(AIControlAdapterHasTickElapsed(100u, 100u), "matching deadline should be considered elapsed");
	expect(AIControlAdapterHasTickElapsed(100u, 101u), "past deadline should be considered elapsed");
	expect(!AIControlAdapterHasTickElapsed(101u, 100u), "future deadline should not be considered elapsed");
	expect(AIControlAdapterIsTickInFuture(101u, 100u), "future deadline should be detected");
	expect(!AIControlAdapterIsTickInFuture(100u, 100u), "current deadline should not be future");
	expect(AIControlAdapterHasTickElapsed(0x7ffffff0u, 0x80000010u), "wrap-safe comparison should treat slightly later ticks as elapsed across the signed boundary");
	expect(!AIControlAdapterHasTickElapsed(0x80000010u, 0x7ffffff0u), "wrap-safe comparison should not treat a slightly later deadline as elapsed before it arrives");
	expect(AIControlAdapterIsTickInFuture(0x80000010u, 0x7ffffff0u), "wrap-safe comparison should detect future deadlines across the signed boundary");
	expect(!AIControlAdapterIsTickInFuture(0x7ffffff0u, 0x80000010u), "elapsed deadlines across the signed boundary should not remain future");

	{
		float dx = 0.0f;
		float dy = 0.0f;
		expect(AIControlAdapterTryNormalizeDirection(3.0f, 4.0f, dx, dy), "direction normalization should accept normal vectors");
		expectNear(dx, 0.6f, 0.0001f, "normalized dx should match expected value");
		expectNear(dy, 0.8f, 0.0001f, "normalized dy should match expected value");
		expectNear((dx * dx) + (dy * dy), 1.0f, 0.0002f, "normalized vector magnitude should be approximately one");
	}

	{
		float dx = 0.0f;
		float dy = 0.0f;
		expect(!AIControlAdapterTryNormalizeDirection(0.0f, 0.0f, dx, dy), "direction normalization should reject zero vectors");
		expect(!AIControlAdapterTryNormalizeDirection(0.5f, 0.5f, dx, dy), "direction normalization should reject near-zero vectors");
	}

	{
		AIControlAdapterMapPoint point = { 0.0f, 0.0f };
		expect(AIControlAdapterTryReadMapPosition(nlohmann::json::object({ { "x", 120.5f }, { "y", -42.0f } }), point), "map position extraction should accept numeric x/y");
		expectNear(point.x, 120.5f, 0.0001f, "map position x should match input");
		expectNear(point.y, -42.0f, 0.0001f, "map position y should match input");
		expect(!AIControlAdapterTryReadMapPosition(nlohmann::json::array(), point), "map position extraction should reject non-object payloads");
		expect(!AIControlAdapterTryReadMapPosition(nlohmann::json::object({ { "x", 1.0f } }), point), "map position extraction should reject missing y");
		expect(!AIControlAdapterTryReadMapPosition(nlohmann::json::object({ { "y", 1.0f } }), point), "map position extraction should reject missing x");
		expect(!AIControlAdapterTryReadMapPosition(nlohmann::json::object({ { "x", "bad" }, { "y", 1.0f } }), point), "map position extraction should reject wrong x types");
	}

	{
		AIControlAdapterMapPoint attackPoint = { 0.0f, 0.0f };
		const nlohmann::json events = nlohmann::json::array({
			nlohmann::json::object({ { "kind", "move" }, { "x", 10.0f }, { "y", 20.0f }, { "tick", 980u } }),
			nlohmann::json::object({ { "kind", "attack" }, { "x", 30.0f }, { "y", 40.0f }, { "tick", 900u } }),
			nlohmann::json::object({ { "kind", "attack" }, { "x", 50.0f }, { "y", 60.0f }, { "tick", 995u } })
		});
		expect(AIControlAdapterTryGetRecentAttackTarget(events, 1000u, 45u, attackPoint), "recent attack target should accept fresh attack events");
		expectNear(attackPoint.x, 50.0f, 0.0001f, "recent attack target should prefer the newest valid event");
		expectNear(attackPoint.y, 60.0f, 0.0001f, "recent attack target y should prefer the newest valid event");
		expect(!AIControlAdapterTryGetRecentAttackTarget(events, 1000u, 2u, attackPoint), "recent attack target should reject stale events");
		expect(!AIControlAdapterTryGetRecentAttackTarget(nlohmann::json::array({
			nlohmann::json::object({ { "kind", "attack" }, { "tick", 995u } })
		}), 1000u, 45u, attackPoint), "recent attack target should reject malformed attack events");
	}

	{
		const AIControlAdapterMapPoint zoneCenter = { 100.0f, 100.0f };
		const AIControlAdapterMapPoint attackPoint = { 130.0f, 100.0f };
		const AIControlAdapterMapPoint preferredEnemyBase = { 100.0f, 200.0f };
		const std::vector<AIControlAdapterMapPoint> enemyBases = {
			{ 300.0f, 100.0f },
			{ 120.0f, 100.0f }
		};

		AIControlAdapterZoneFrontDirectionResult result = AIControlAdapterResolveZoneFrontDirection(
			zoneCenter,
			true,
			attackPoint,
			true,
			preferredEnemyBase,
			enemyBases,
			0.0f,
			-1.0f);
		expect(std::string(result.source) == "attack_target", "front direction should prefer a recent attack target");
		expectNear(result.dx, 1.0f, 0.0001f, "attack-target front direction should point at the attack target");
		expectNear(result.dy, 0.0f, 0.0001f, "attack-target front direction should point at the attack target");

		result = AIControlAdapterResolveZoneFrontDirection(
			zoneCenter,
			false,
			attackPoint,
			true,
			preferredEnemyBase,
			enemyBases,
			0.0f,
			-1.0f);
		expect(std::string(result.source) == "enemy_base", "front direction should fall back to preferred enemy base");
		expectNear(result.dx, 0.0f, 0.0001f, "preferred enemy-base front direction should point vertically");
		expectNear(result.dy, 1.0f, 0.0001f, "preferred enemy-base front direction should point vertically");

		result = AIControlAdapterResolveZoneFrontDirection(
			zoneCenter,
			false,
			attackPoint,
			false,
			preferredEnemyBase,
			enemyBases,
			0.0f,
			-1.0f);
		expect(std::string(result.source) == "enemy_base", "front direction should fall back to nearest known enemy base");
		expectNear(result.dx, 1.0f, 0.0001f, "nearest enemy-base front direction should choose the closest base");
		expectNear(result.dy, 0.0f, 0.0001f, "nearest enemy-base front direction should choose the closest base");

		result = AIControlAdapterResolveZoneFrontDirection(
			zoneCenter,
			false,
			attackPoint,
			false,
			preferredEnemyBase,
			std::vector<AIControlAdapterMapPoint>{ { 100.0f, 100.0f } },
			0.0f,
			-1.0f);
		expect(std::string(result.source) == "sprawl_axis", "front direction should fall back to the sprawl axis when other sources are degenerate");
		expectNear(result.dx, 0.0f, 0.0001f, "sprawl-axis fallback dx should be preserved");
		expectNear(result.dy, -1.0f, 0.0001f, "sprawl-axis fallback dy should be preserved");
	}

	{
		const AIControlAdapterZoneFrontRearPoints points = AIControlAdapterBuildZoneFrontRearPoints(
			{ 10.0f, 20.0f },
			5.0f,
			0.6f,
			0.8f);
		expectNear(points.frontPoint.x, 13.0f, 0.0001f, "front point x should be center plus direction times radius");
		expectNear(points.frontPoint.y, 24.0f, 0.0001f, "front point y should be center plus direction times radius");
		expectNear(points.rearPoint.x, 7.0f, 0.0001f, "rear point x should be center minus direction times radius");
		expectNear(points.rearPoint.y, 16.0f, 0.0001f, "rear point y should be center minus direction times radius");
	}

	// Test zone expansion urgency (B031)
	{
		// Large gap scenario: 3 zones vs 30 desired
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			3,   // currentZoneCount
			30,  // desiredZoneCount
			5    // zoneGapThreshold
		};
		expect(AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should be urgent when 27 zones below target");
	}

	{
		// Small gap scenario: 8 zones vs 10 desired (gap = 2, below threshold of 5)
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			8,   // currentZoneCount
			10,  // desiredZoneCount
			5    // zoneGapThreshold
		};
		expect(!AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should not be urgent when gap is small");
	}

	{
		// At target scenario: 10 zones vs 10 desired
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			10,  // currentZoneCount
			10,  // desiredZoneCount
			5    // zoneGapThreshold
		};
		expect(!AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should not be urgent when at target");
	}

	{
		// Threshold boundary: exactly 5 zones gap
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			5,   // currentZoneCount
			10,  // desiredZoneCount
			5    // zoneGapThreshold
		};
		expect(AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should be urgent when gap equals threshold");
	}

	{
		// Phase 5.7 scenario: live run stalled at 7 zones vs 30 desired
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			7,   // currentZoneCount (matches live run)
			30,  // desiredZoneCount (sprawl_balanced target)
			5    // zoneGapThreshold
		};
		expect(AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should be urgent when 23 zones below target (Phase 5.7 scenario)");
	}

	{
		// Zone gap just below threshold: 4 zones gap
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			11,  // currentZoneCount
			15,  // desiredZoneCount
			5    // zoneGapThreshold
		};
		expect(!AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should not be urgent when gap is just below threshold");
	}

	{
		// High zone count but still below target: 25 zones vs 30 desired
		const AIControlAdapterZoneExpansionPolicyInputs inputs = {
			25,  // currentZoneCount
			30,  // desiredZoneCount
			5    // zoneGapThreshold
		};
		expect(AIControlAdapterIsZoneExpansionUrgent(inputs), "zone expansion should be urgent even at high absolute counts when gap >= threshold");
	}

	// =========================================================================
	// Phase 5.7: Zone Expansion Arbitration Tests
	// =========================================================================

	{
		// Live scenario: 7 current / 30 desired with high cash above reserve
		// Should select urgent expansion despite reserve protection
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent (gap=23 >= threshold 5)
			true,   // allowUrgentExpansionDespiteReserve (cash_float >= 10000)
			false,  // remoteZoneNeedsFollowup
			7,      // stashZoneCount
			30,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			120000, // money (high cash)
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(result.shouldAttemptExpansion, "Phase 5.7: urgent expansion should be selected with large gap and high cash");
		expect(result.isUrgent, "Phase 5.7: expansion should be marked as urgent");
		expect(result.allowReserveSpend, "Phase 5.7: urgent expansion should allow reserve spend");
		expect(std::strcmp(result.reason, "urgent_high_cash") == 0, "Phase 5.7: reason should be urgent_high_cash");
	}

	{
		// Urgent expansion even with remoteZoneNeedsFollowup
		// When zone deficit is critical, we can start new zone despite followup needed
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			true,   // remoteZoneNeedsFollowup (true but urgent overrides)
			5,      // stashZoneCount
			25,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			150000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(result.shouldAttemptExpansion, "Phase 5.7: urgent expansion should override followup requirement");
		expect(result.isUrgent, "Phase 5.7: should be urgent with large gap");
	}

	{
		// Normal expansion (not urgent): small gap below threshold
		// Should respect normal rules (followup must be complete)
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			false,  // zoneExpansionIsUrgent (gap=3 < threshold 5)
			false,  // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			12,     // stashZoneCount
			15,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			50000,  // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(result.shouldAttemptExpansion, "Phase 5.7: normal expansion should proceed when followup complete");
		expect(!result.isUrgent, "Phase 5.7: should not be urgent with small gap");
		expect(std::strcmp(result.reason, "normal") == 0, "Phase 5.7: reason should be normal");
	}

	{
		// Normal expansion blocked by followup needed
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			false,  // zoneExpansionIsUrgent
			false,  // allowUrgentExpansionDespiteReserve
			true,   // remoteZoneNeedsFollowup (blocks normal expansion)
			12,     // stashZoneCount
			15,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			50000,  // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 5.7: normal expansion should be blocked by followup");
		expect(std::strcmp(result.reason, "followup_needed") == 0, "Phase 5.7: reason should be followup_needed");
	}

	{
		// Expansion in progress: should produce specific blocker
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			7,      // stashZoneCount
			30,     // desiredZoneCount
			1,      // supplyStashesInProgress (blocker)
			false,  // shouldThrottleExtraStashGrowth
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 5.7: expansion should be blocked when build in progress");
		expect(std::strcmp(result.reason, "build_in_progress") == 0, "Phase 5.7: reason should be build_in_progress");
	}

	{
		// Phase 12: profile-driven concurrent expansion can keep seeding zones
		// when urgent expansion is active and the in-progress count is below cap.
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			true,   // remoteZoneNeedsFollowup (urgent overrides)
			7,      // stashZoneCount
			30,     // desiredZoneCount
			1,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true,   // isBuildAttemptReady
			3       // maxConcurrentSupplyStashes
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(result.shouldAttemptExpansion, "Phase 12: urgent expansion should allow another stash below concurrent cap");
		expect(result.isUrgent, "Phase 12: concurrent expansion should preserve urgent classification");
		expect(std::strcmp(result.reason, "urgent_high_cash") == 0, "Phase 12: concurrent expansion should keep urgent_high_cash reason");
	}

	{
		// Phase 12: in-progress count below cap is not enough by itself;
		// retry cooldown still blocks repeated command spam.
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			true,   // remoteZoneNeedsFollowup
			10,     // stashZoneCount
			30,     // desiredZoneCount
			1,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			false,  // isBuildAttemptReady/cooldown ready
			3       // maxConcurrentSupplyStashes
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 12: expansion should still respect cooldown below concurrent cap");
		expect(std::strcmp(result.reason, "build_cooldown") == 0, "Phase 12: cooldown block should report build_cooldown");
	}

	{
		// Phase 12: once the configured concurrent cap is reached, block explicitly.
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			7,      // stashZoneCount
			30,     // desiredZoneCount
			3,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true,   // isBuildAttemptReady
			3       // maxConcurrentSupplyStashes
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 12: expansion should block when concurrent stash cap is reached");
		expect(std::strcmp(result.reason, "in_progress_cap") == 0, "Phase 12: cap block should report in_progress_cap");
	}

	{
		const AIControlAdapterMacroExpansionSnapshot snapshot = {
			true,    // isSprawlStyle
			true,    // isBalancedSprawl
			10,      // stashZoneCount
			7,       // developedZoneCount
			3,       // remoteSupplyZoneCount
			1800.0f, // supplyFootprintRadius
			30,      // desiredZoneCount
			5,       // urgentZoneGapThreshold
			5,       // supplyStashesInProgress
			3,       // maxConcurrentSupplyStashes
			false,   // remoteZoneNeedsFollowup
			false,   // shouldThrottleExtraStashGrowth
			true,    // isBuildCooldownReady
			16000u,  // money
			10000u,  // reserveCash
			10000u   // expansionHighCashFloatThreshold
		};
		const auto decision = AIControlAdapterResolveMacroExpansionDecision(snapshot);
		expect(decision.zoneExpansionUrgent, "Phase 12 follow-up: large zone gap should be urgent");
		expect(decision.maxConcurrentExpansionStashes == 6, "Phase 12 follow-up: protected urgent expansion should raise cap to six");
		expect(std::strcmp(decision.concurrencyReason, "urgent_reserve_protected") == 0, "Phase 12 follow-up: protected cap reason should be explicit");
		expect(decision.arbitration.shouldAttemptExpansion, "Phase 12 follow-up: five in progress should still allow the sixth expansion");
	}

	{
		const AIControlAdapterMacroExpansionSnapshot snapshot = {
			true,    // isSprawlStyle
			true,    // isBalancedSprawl
			10,      // stashZoneCount
			7,       // developedZoneCount
			3,       // remoteSupplyZoneCount
			1800.0f, // supplyFootprintRadius
			30,      // desiredZoneCount
			5,       // urgentZoneGapThreshold
			7,       // supplyStashesInProgress
			3,       // maxConcurrentSupplyStashes
			false,   // remoteZoneNeedsFollowup
			false,   // shouldThrottleExtraStashGrowth
			true,    // isBuildCooldownReady
			40000u,  // money
			10000u,  // reserveCash
			10000u   // expansionHighCashFloatThreshold
		};
		const auto decision = AIControlAdapterResolveMacroExpansionDecision(snapshot);
		expect(decision.maxConcurrentExpansionStashes == 8, "Phase 12 follow-up: high-cash urgent expansion should raise cap to eight");
		expect(std::strcmp(decision.concurrencyReason, "urgent_high_cash") == 0, "Phase 12 follow-up: high-cash cap reason should be explicit");
		expect(decision.arbitration.shouldAttemptExpansion, "Phase 12 follow-up: seven in progress should still allow the eighth expansion");
	}

	{
		const AIControlAdapterMacroExpansionSnapshot snapshot = {
			true,    // isSprawlStyle
			true,    // isBalancedSprawl
			10,      // stashZoneCount
			7,       // developedZoneCount
			3,       // remoteSupplyZoneCount
			1800.0f, // supplyFootprintRadius
			30,      // desiredZoneCount
			5,       // urgentZoneGapThreshold
			8,       // supplyStashesInProgress
			3,       // maxConcurrentSupplyStashes
			false,   // remoteZoneNeedsFollowup
			false,   // shouldThrottleExtraStashGrowth
			true,    // isBuildCooldownReady
			40000u,  // money
			10000u,  // reserveCash
			10000u   // expansionHighCashFloatThreshold
		};
		const auto decision = AIControlAdapterResolveMacroExpansionDecision(snapshot);
		expect(!decision.arbitration.shouldAttemptExpansion, "Phase 12 follow-up: eight in progress should hit the high-cash cap");
		expect(std::strcmp(decision.arbitration.reason, "in_progress_cap") == 0, "Phase 12 follow-up: cap block should remain explicit");
	}

	{
		// Phase 12: many local supply zones can still be strategically insignificant
		// when the footprint has not left the home quadrant.
		const AIControlAdapterMacroExpansionSnapshot snapshot = {
			true,    // isSprawlStyle
			true,    // isBalancedSprawl
			10,      // stashZoneCount
			7,       // developedZoneCount
			1,       // remoteSupplyZoneCount
			900.0f,  // supplyFootprintRadius
			30,      // desiredZoneCount
			5,       // urgentZoneGapThreshold
			1,       // supplyStashesInProgress
			3,       // maxConcurrentSupplyStashes
			true,    // remoteZoneNeedsFollowup
			false,   // shouldThrottleExtraStashGrowth
			true,    // isBuildCooldownReady
			50000u,  // money
			10000u,  // reserveCash
			10000u   // expansionHighCashFloatThreshold
		};
		const auto decision = AIControlAdapterResolveMacroExpansionDecision(snapshot);
		expect(decision.coverageExpansionUrgent, "Phase 12: low footprint should make expansion urgent despite local zone count");
		expect(decision.zoneExpansionUrgent, "Phase 12: coverage urgency should set combined urgency");
		expect(decision.preferRemoteSupplyExpansion, "Phase 12: poor footprint should prefer remote supply selection");
		expect(decision.arbitration.shouldAttemptExpansion, "Phase 12: poor-footprint high-cash expansion should attempt another stash");
		expect(std::strcmp(decision.expansionReason, "coverage_gap_high_cash") == 0, "Phase 12: reason should identify coverage gap");
	}

	{
		// Phase 12: enough total zones but too few remote supply zones should still
		// bias expansion toward far supply docks while under target.
		const AIControlAdapterMacroExpansionSnapshot snapshot = {
			true,    // isSprawlStyle
			true,    // isBalancedSprawl
			24,      // stashZoneCount
			18,      // developedZoneCount
			2,       // remoteSupplyZoneCount
			2600.0f, // supplyFootprintRadius
			30,      // desiredZoneCount
			5,       // urgentZoneGapThreshold
			0,       // supplyStashesInProgress
			3,       // maxConcurrentSupplyStashes
			false,   // remoteZoneNeedsFollowup
			false,   // shouldThrottleExtraStashGrowth
			true,    // isBuildCooldownReady
			50000u,  // money
			10000u,  // reserveCash
			10000u   // expansionHighCashFloatThreshold
		};
		const auto decision = AIControlAdapterResolveMacroExpansionDecision(snapshot);
		expect(decision.desiredRemoteSupplyZones == 7, "Phase 12: desired remote zones should scale from desired zone count");
		expect(decision.coverageExpansionUrgent, "Phase 12: remote supply deficit should be urgent");
		expect(decision.preferRemoteSupplyExpansion, "Phase 12: remote supply deficit should prefer remote selection");
		expect(decision.arbitration.shouldAttemptExpansion, "Phase 12: remote supply deficit should allow expansion when spend conditions are met");
	}

	{
		// Phase 12: footprint urgency still respects the reserve/high-cash gate.
		const AIControlAdapterMacroExpansionSnapshot snapshot = {
			true,    // isSprawlStyle
			true,    // isBalancedSprawl
			10,      // stashZoneCount
			7,       // developedZoneCount
			1,       // remoteSupplyZoneCount
			900.0f,  // supplyFootprintRadius
			30,      // desiredZoneCount
			5,       // urgentZoneGapThreshold
			1,       // supplyStashesInProgress
			3,       // maxConcurrentSupplyStashes
			true,    // remoteZoneNeedsFollowup
			false,   // shouldThrottleExtraStashGrowth
			true,    // isBuildCooldownReady
			10500u,  // money
			10000u,  // reserveCash
			10000u   // expansionHighCashFloatThreshold
		};
		const auto decision = AIControlAdapterResolveMacroExpansionDecision(snapshot);
		expect(decision.coverageExpansionUrgent, "Phase 12: coverage urgency should be detected even with low cash float");
		expect(!decision.allowUrgentExpansionDespiteReserve, "Phase 12: low cash float should not allow urgent reserve spend");
		expect(!decision.arbitration.shouldAttemptExpansion, "Phase 12: low cash float should block remote followup override");
		expect(std::strcmp(decision.expansionReason, "coverage_gap_low_cash") == 0, "Phase 12: reason should identify low-cash coverage gap");
	}

	{
		const AIControlAdapterZoneSeedPackageDecision decision = AIControlAdapterChooseZoneSeedPackage({
			true,  // isSprawlStyle
			true,  // canScaleMilitaryProduction
			true,  // remoteZoneHasStash
			false, // coverageExpansionUrgent
			false, // activeZoneThreatened
			true,  // allowExpansionBeforeFullRemoteFollowup
			0,     // tunnels
			0,     // tunnelsInProgress
			0,     // stingers
			0,     // stingersInProgress
			0,     // barracks
			0,     // barracksInProgress
			0,     // armsDealers
			0      // armsDealersInProgress
		});
		expect(decision.needsFollowup, "Phase 12: new remote stash should need followup");
		expect(std::strcmp(decision.packageStage, "tunnel") == 0, "Phase 12: seed package should build tunnel first");
		expect(std::strcmp(decision.command, "Game.BuildTunnelNetwork") == 0, "Phase 12: tunnel stage should produce tunnel command");
	}

	{
		const AIControlAdapterZoneSeedPackageDecision decision = AIControlAdapterChooseZoneSeedPackage({
			true,  // isSprawlStyle
			true,  // canScaleMilitaryProduction
			true,  // remoteZoneHasStash
			false, // coverageExpansionUrgent
			false, // activeZoneThreatened
			true,  // allowExpansionBeforeFullRemoteFollowup
			1,     // tunnels
			0,     // tunnelsInProgress
			0,     // stingers
			0,     // stingersInProgress
			0,     // barracks
			0,     // barracksInProgress
			0,     // armsDealers
			0      // armsDealersInProgress
		});
		expect(decision.needsFollowup, "Phase 12: tunneled remote stash should need stinger followup");
		expect(std::strcmp(decision.packageStage, "stinger") == 0, "Phase 12: seed package should build stinger after tunnel");
		expect(std::strcmp(decision.command, "Game.BuildStingerSite") == 0, "Phase 12: stinger stage should produce stinger command");
	}

	{
		const AIControlAdapterZoneSeedPackageDecision decision = AIControlAdapterChooseZoneSeedPackage({
			true,  // isSprawlStyle
			true,  // canScaleMilitaryProduction
			true,  // remoteZoneHasStash
			true,  // coverageExpansionUrgent
			false, // activeZoneThreatened
			true,  // allowExpansionBeforeFullRemoteFollowup
			1,     // tunnels
			0,     // tunnelsInProgress
			1,     // stingers
			0,     // stingersInProgress
			0,     // barracks
			0,     // barracksInProgress
			0,     // armsDealers
			0      // armsDealersInProgress
		});
		expect(!decision.needsFollowup, "Phase 12: coverage urgency should skip non-critical producer followup after basic defense");
		expect(decision.command == nullptr, "Phase 12: coverage-priority seed decision should not emit producer command");
		expect(std::strcmp(decision.reason, "coverage_expansion_priority") == 0, "Phase 12: skipped producer followup should explain coverage priority");
	}

	{
		const AIControlAdapterZoneSeedPackageDecision decision = AIControlAdapterChooseZoneSeedPackage({
			true,  // isSprawlStyle
			true,  // canScaleMilitaryProduction
			true,  // remoteZoneHasStash
			true,  // coverageExpansionUrgent
			true,  // activeZoneThreatened
			true,  // allowExpansionBeforeFullRemoteFollowup
			1,     // tunnels
			0,     // tunnelsInProgress
			0,     // stingers
			0,     // stingersInProgress
			0,     // barracks
			0,     // barracksInProgress
			0,     // armsDealers
			0      // armsDealersInProgress
		});
		expect(decision.needsFollowup, "Phase 12: threatened zone should still receive defensive followup under coverage pressure");
		expect(std::strcmp(decision.command, "Game.BuildStingerSite") == 0, "Phase 12: threatened tunneled zone should build stinger");
		expect(std::strcmp(decision.reason, "threatened_needs_stinger") == 0, "Phase 12: threatened defensive followup should be explicit");
	}

	{
		// Throttle produces specific blocker
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			7,      // stashZoneCount
			30,     // desiredZoneCount
			0,      // supplyStashesInProgress
			true,   // shouldThrottleExtraStashGrowth (blocker)
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 5.7: expansion should be blocked when throttled");
		expect(std::strcmp(result.reason, "throttled") == 0, "Phase 5.7: reason should be throttled");
	}

	{
		// Target reached produces specific reason
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			false,  // zoneExpansionIsUrgent (at target)
			false,  // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			30,     // stashZoneCount (equals desired)
			30,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 5.7: expansion should not be attempted when target reached");
		expect(std::strcmp(result.reason, "target_reached") == 0, "Phase 5.7: reason should be target_reached");
	}

	{
		// Urgent but low cash produces specific reason
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			7,      // stashZoneCount
			30,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			1500,   // money (below 2200 for balanced sprawl)
			10000,  // reserveCash
			true,   // isBalancedSprawl
			true    // isBuildAttemptReady
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 5.7: urgent expansion should be blocked when cash too low");
		expect(std::strcmp(result.reason, "urgent_low_cash") == 0, "Phase 5.7: reason should be urgent_low_cash");
	}

	{
		// Build cooldown active produces specific reason
		const AIControlAdapterZoneExpansionArbitrationInputs inputs = {
			true,   // zoneExpansionIsUrgent
			true,   // allowUrgentExpansionDespiteReserve
			false,  // remoteZoneNeedsFollowup
			7,      // stashZoneCount
			30,     // desiredZoneCount
			0,      // supplyStashesInProgress
			false,  // shouldThrottleExtraStashGrowth
			120000, // money
			10000,  // reserveCash
			true,   // isBalancedSprawl
			false   // isBuildAttemptReady (blocker)
		};
		const auto result = AIControlAdapterChooseZoneExpansionAction(inputs);
		expect(!result.shouldAttemptExpansion, "Phase 5.7: expansion should be blocked when build cooldown active");
		expect(std::strcmp(result.reason, "build_cooldown") == 0, "Phase 5.7: reason should be build_cooldown");
	}

	// =========================================================================
	// Phase 6.3: Capture Source Capacity and Concurrency Tests
	// =========================================================================

	// Test: No capture upgrade means no forced Rebel reserve
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false, // shouldPauseForEconomy
			nullptr,
			false, // shouldHoldArmyCap
			true,  // isBalancedSprawl
			"balanced",
			5000u, // money
			1,     // barracks
			1,     // armsDealers
			0,     // palaces
			false,
			false,
			false, // hasCaptureUpgrade
			0,     // captureSources
			0,     // captureSourcesLive
			0,     // captureSourcesReserved
			0,     // captureSourcesAvailable
			5,     // capturableTargetsRemaining
			3,     // desiredCaptureSources (would be desired if upgrade existed)
			2,     // maxCaptureConcurrent
			0,     // soldiers
			0,     // rpg
			0,     // quads
			0,     // scorpions
			0,     // scudLaunchers
			0,     // radarVans
			0,     // armyCount
			20     // armyCap
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should NOT force Game.QueueSoldiersAllBarracks for capture reserve
		// (may queue soldiers for other reasons, but not because of capture capacity)
		const bool isForcedForCapture = result.command != nullptr
			&& std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0
			&& (result.reason == nullptr || std::strlen(result.reason) == 0 || std::strcmp(result.reason, "capture_utility_reserve") == 0);
		expect(!isForcedForCapture,
			"Phase 6.3: no capture upgrade should not force Rebel reserve");
	}

	// Test: No capturable targets means no forced Rebel reserve
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			false,
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,  // hasCaptureUpgrade
			0,
			0,
			0,
			0,
			0,     // capturableTargetsRemaining
			0,     // desiredCaptureSources
			2,
			0,
			0,
			0,
			0,
			0,
			0,
			0,
			20
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should NOT force Game.QueueSoldiersAllBarracks when no capturable targets remain
		// When capturableTargetsRemaining == 0, desiredCaptureSources should be 0,
		// so no capture reserve production should occur
		const bool isForcedForCapture = result.command != nullptr
			&& std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0
			&& (result.reason == nullptr || std::strlen(result.reason) == 0 || std::strcmp(result.reason, "capture_utility_reserve") == 0);
		expect(!isForcedForCapture,
			"Phase 6.3: no capturable targets should not force Rebel reserve or soldiers for capture capacity");
	}

	// Test: One live Rebel with max_concurrent=2 and targets forces another
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			false,
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,
			1,     // captureSources
			1,     // captureSourcesLive
			0,     // captureSourcesReserved
			1,     // captureSourcesAvailable
			5,     // capturableTargetsRemaining
			3,     // desiredCaptureSources = min(2+1, 5) = 3
			2,     // maxCaptureConcurrent
			1,     // soldiers (the one Rebel)
			0,
			0,
			0,
			0,
			0,
			1,     // armyCount
			20
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should build soldiers for capture capacity: available(1) < desired(3)
		expect(result.command != nullptr && std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0,
			"Phase 6.3: should force soldiers when available < desired");
	}

	// Test: Reserved Rebel counts as unavailable
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			false,
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,
			1,     // captureSources
			1,     // captureSourcesLive
			1,     // captureSourcesReserved
			0,     // captureSourcesAvailable = live - reserved
			5,     // capturableTargetsRemaining
			3,     // desiredCaptureSources
			2,
			1,
			0,
			0,
			0,
			0,
			0,
			1,
			20
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should force production: available(0) < desired(3)
		expect(result.command != nullptr && std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0,
			"Phase 6.3: reserved Rebel should count as unavailable, forcing production");
	}

	// Test: Automation disabled (desiredCaptureSources == 0) means no forced reserve
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			false,
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,  // hasCaptureUpgrade
			0,
			0,
			0,
			0,
			5,     // capturableTargetsRemaining (targets exist but automation disabled)
			0,     // desiredCaptureSources (automation disabled)
			2,
			0,
			0,
			0,
			0,
			0,
			0,
			0,
			20
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should NOT force capture reserve when automation disabled
		const bool isForcedForCapture = result.command != nullptr
			&& std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0
			&& (result.reason == nullptr || std::strlen(result.reason) == 0 || std::strcmp(result.reason, "capture_utility_reserve") == 0);
		expect(!isForcedForCapture,
			"Phase 6.3: automation disabled (desiredCaptureSources == 0) should not force Rebel reserve");
	}

	// Test: Skewed vehicle/infantry mix still forces capture reserve
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			false,
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,
			0,     // captureSources
			0,     // captureSourcesLive
			0,     // captureSourcesReserved
			0,     // captureSourcesAvailable
			5,     // capturableTargetsRemaining
			3,     // desiredCaptureSources
			2,
			0,     // soldiers
			0,     // rpg
			0,     // quads (vehicle-starved)
			0,     // scorpions
			0,
			0,
			0,     // armyCount
			100    // armyCap (plenty of room)
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should force soldiers for capture reserve even when vehicle count is low
		// Capture reserve overrides composition when available < desired
		expect(result.command != nullptr && std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0,
			"Phase 6.3: capture reserve should override composition priorities when available < desired");
	}

	// Test: Available >= desired does not force extra Rebels
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			false,
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,
			3,     // captureSources
			3,     // captureSourcesLive
			0,     // captureSourcesReserved
			3,     // captureSourcesAvailable
			5,     // capturableTargetsRemaining
			3,     // desiredCaptureSources
			2,
			3,     // soldiers
			0,
			0,
			0,
			0,
			0,
			3,
			20
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should not force capture reserve: available(3) >= desired(3)
		// May build other units or nothing
		expect(result.command == nullptr
			|| std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") != 0
			|| (result.reason != nullptr && std::strcmp(result.reason, "capture_utility_reserve") != 0),
			"Phase 6.3: should not force Rebel when available >= desired");
	}

	// Test: Army cap override for bounded capture reserve
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			true,  // shouldHoldArmyCap
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,
			0,     // captureSources
			0,     // captureSourcesLive
			0,     // captureSourcesReserved
			0,     // captureSourcesAvailable
			5,     // capturableTargetsRemaining
			3,     // desiredCaptureSources
			2,
			0,
			0,
			0,
			0,
			0,
			0,
			20,    // armyCount
			20     // armyCap (at cap)
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should override army cap for capture utility reserve
		expect(result.command != nullptr && std::strcmp(result.command, "Game.QueueSoldiersAllBarracks") == 0,
			"Phase 6.3: should override army cap for bounded capture reserve");
		expect(result.reason != nullptr && std::strcmp(result.reason, "capture_utility_reserve") == 0,
			"Phase 6.3: reason should be capture_utility_reserve");
	}

	// Test: Bounded override does not allow infinite production
	{
		const AIControlAdapterProductionChoiceInputs inputs = {
			false,
			nullptr,
			true,  // shouldHoldArmyCap
			true,
			"balanced",
			5000u,
			1,
			1,
			0,
			false,
			false,
			true,
			0,
			0,
			0,
			0,
			5,
			3,     // desiredCaptureSources
			2,
			0,
			0,
			0,
			0,
			0,
			0,
			25,    // armyCount = armyCap(20) + desired(3) + 2 (exceeded bound)
			20
		};
		const auto result = AIControlAdapterChoosePreferredProductionCommand(inputs);
		// Should NOT produce when army exceeds cap + desired reserve
		expect(result.command == nullptr,
			"Phase 6.3: should not produce beyond armyCap + desiredCaptureSources");
		expect(result.reason != nullptr && std::strcmp(result.reason, "army_cap_reached") == 0,
			"Phase 6.3: should return army_cap_reached when exceeded bound");
	}

	{
		const AIControlAdapterScudStormConstructionPolicyInputs inputs = {
			true,      // prereqReady
			true,      // productionNeeded
			12000u,    // money
			10000u,    // reserveCash
			5000u,     // scudStormCost
			10,
			10,
			5,
			0,
			1,
			25000u
		};
		const auto result = AIControlAdapterEvaluateScudStormConstruction(inputs);
		expect(!result.spendAllowed, "SCUD Storm construction should not spend reserve cash");
		expect(std::strcmp(result.reason, "reserve_protected") == 0,
			"SCUD Storm construction should report reserve_protected below reserve plus cost");
	}

	{
		const AIControlAdapterScudStormConstructionPolicyInputs inputs = {
			true,
			true,
			25000u,
			10000u,
			5000u,
			3,      // current zones
			10,     // desired zones, gap 7
			5,
			0,
			1,
			25000u
		};
		const auto result = AIControlAdapterEvaluateScudStormConstruction(inputs);
		expect(!result.spendAllowed, "Urgent expansion should block SCUD Storm construction without high cash float");
		expect(result.zoneExpansionUrgent, "SCUD Storm policy should use the zone expansion urgency threshold");
		expect(std::strcmp(result.reason, "urgent_expansion_priority") == 0,
			"SCUD Storm construction should report urgent expansion priority");
	}

	{
		const AIControlAdapterScudStormConstructionPolicyInputs inputs = {
			true,
			true,
			35000u,
			10000u,
			5000u,
			3,
			10,
			5,
			0,
			1,
			25000u
		};
		const auto result = AIControlAdapterEvaluateScudStormConstruction(inputs);
		expect(result.spendAllowed, "Very high cash float should allow SCUD Storm construction despite urgent expansion");
		expect(result.highCashOverride, "Very high cash float should be tagged as a high-cash override");
		expect(result.maxInProgress == 2, "Very high cash float should allow two in-progress SCUD Storms");
		expect(std::strcmp(result.reason, "high_cash_override") == 0,
			"SCUD Storm construction should report high_cash_override");
	}

	{
		const AIControlAdapterScudStormConstructionPolicyInputs inputs = {
			true,
			true,
			20000u,
			10000u,
			5000u,
			10,
			10,
			5,
			1,
			1,
			25000u
		};
		const auto result = AIControlAdapterEvaluateScudStormConstruction(inputs);
		expect(!result.spendAllowed, "Normal SCUD Storm in-progress cap should block another build");
		expect(std::strcmp(result.reason, "in_progress_cap") == 0,
			"SCUD Storm construction should report in_progress_cap");
	}

	{
		const AIControlAdapterScudStormConstructionPolicyInputs inputs = {
			true,
			false,
			100000u,
			10000u,
			5000u,
			3,
			10,
			5,
			0,
			1,
			25000u
		};
		const auto result = AIControlAdapterEvaluateScudStormConstruction(inputs);
		expect(!result.spendAllowed, "SCUD Storm construction gate should block only new construction when target is reached");
		expect(std::strcmp(result.reason, "target_reached") == 0,
			"SCUD Storm construction should report target_reached independently of firing policy");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 12000u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 5000u;
		inputs.completedMarkets = 0;
		inputs.incomeCritical = true;
		inputs.reserveDepleted = false;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::LuxuryBaseline, inputs);
		expect(!result.allowed, "strategic spend should block luxury spending while recovery cash is protected");
		expect(std::strcmp(result.reason, "recovery_cash_protected") == 0,
			"strategic spend should report recovery cash protection for luxury blocks");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 2600u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 2500u;
		inputs.completedMarkets = 0;
		inputs.staleMarketFoundations = 1;
		inputs.incomeCritical = true;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::EconomyRecovery, inputs);
		expect(result.allowed, "strategic spend should allow replacing stale Black Market foundations");
		expect(std::strcmp(result.reason, "replace_stale_income_foundation") == 0,
			"strategic spend should report stale income replacement");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 50000u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 2500u;
		inputs.completedMarkets = 1;
		inputs.healthyMarketsInProgress = 1;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::EconomyRecovery, inputs);
		expect(!result.allowed, "strategic spend should block duplicate recovery while a healthy Black Market is in progress");
		expect(std::strcmp(result.reason, "healthy_income_build_in_progress") == 0,
			"strategic spend should distinguish healthy income builds from stale foundations");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 1700u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 700u;
		inputs.completedMarkets = 0;
		inputs.activeLocalEnemies = 3;
		inputs.emergencySurvivalActive = true;
		inputs.incomeCritical = true;
		inputs.reserveDepleted = true;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::EmergencyDefenseUnits, inputs);
		expect(result.allowed, "strategic spend should allow bounded emergency defense pulses under reserve pressure");
		expect(result.batchLimit == 1, "reserve-pressure emergency defense should be capped to one pulse");
		expect(std::strcmp(result.reason, "bounded_emergency_pulse") == 0,
			"bounded emergency defense should report bounded_emergency_pulse");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 700u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 700u;
		inputs.activeLocalEnemies = 5;
		inputs.mainBaseCritical = true;
		inputs.emergencySurvivalActive = true;
		inputs.quads = 30;
		inputs.scorpions = 1;
		inputs.buggies = 1;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::EmergencyDefenseUnits, inputs);
		expect(result.allowed, "main-base critical emergency should override normal quad saturation");
		expect(result.batchLimit == 3, "main-base critical emergency should allow a larger explicit batch");
		expect(std::strcmp(result.reason, "main_base_critical_override") == 0,
			"main-base critical emergency should report override reason");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 50000u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 700u;
		inputs.activeLocalEnemies = 2;
		inputs.emergencySurvivalActive = true;
		inputs.quads = 30;
		inputs.scorpions = 2;
		inputs.buggies = 1;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::EmergencyDefenseUnits, inputs);
		expect(!result.allowed, "quad-heavy armies should not keep spending emergency reserve on more quads without collapse pressure");
		expect(result.batchLimit == 0, "quad saturation should suppress the emergency batch");
		expect(std::strcmp(result.reason, "quad_saturation_recovery_protected") == 0,
			"quad saturation should report specialist/recovery protection");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 17000u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 5000u;
		inputs.completedMarkets = 1;
		inputs.expansionUrgent = true;
		inputs.incomeCritical = false;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::LuxuryBaseline, inputs);
		expect(!result.allowed, "urgent expansion should block luxury baseline strategic spending");
		expect(std::strcmp(result.reason, "urgent_expansion_priority") == 0,
			"urgent expansion should be visible as the block reason");
	}

	{
		AIControlAdapterStrategicSpendInput inputs;
		inputs.money = 22000u;
		inputs.reserveCash = 10000u;
		inputs.requestCost = 5000u;
		inputs.completedMarkets = 1;
		inputs.activeWmdThreats = 1;
		const auto result = AIControlAdapterEvaluateStrategicSpend(StrategicSpendCategory::DefensiveWmd, inputs);
		expect(result.allowed, "defensive WMD spending should be allowed when reserve is protected");
		expect(std::strcmp(result.reason, "defensive_wmd") == 0,
			"defensive WMD spending should report defensive_wmd");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.hasActiveWmdTarget = true;
		inputs.candidates.push_back(makeScudStormTarget(100u, "base/command", "ChinaCommandCenter", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(!result.hasTarget, "Enemy WMD targets should preempt strategic SCUD Storm fallback targets");
		expect(std::strcmp(result.reason, "enemy_wmd_preempts") == 0,
			"Strategic SCUD Storm target selection should report enemy_wmd_preempts");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.candidates.push_back(makeScudStormTarget(201u, "production", "ChinaWarFactory", true, 0u));
		inputs.candidates.push_back(makeScudStormTarget(200u, "base/command", "ChinaCommandCenter", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(result.hasTarget && result.objectId == 200u, "Ready SCUD Storm should prefer enemy command structures");
		expect(result.targetKind == "base/command", "Command target should be reported as base/command");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.candidates.push_back(makeScudStormTarget(301u, "economy", "GLASupplyStash", true, 0u));
		inputs.candidates.push_back(makeScudStormTarget(300u, "production", "GLAArmsDealer", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(result.hasTarget && result.objectId == 300u, "SCUD Storm strategic fallback should prefer production over economy");
		expect(result.targetKind == "production", "Production fallback should report production kind");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.candidates.push_back(makeScudStormTarget(400u, "production", "ChinaWarFactory", false, 10000u));
		inputs.candidates.push_back(makeScudStormTarget(401u, "economy", "ChinaSupplyCenter", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(result.hasTarget && result.objectId == 401u, "Visible high-value targets should beat stale strategic targets");
		expect(result.visible, "Selected strategic target should be visible when a visible high-value target exists");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.candidates.push_back(makeScudStormTarget(500u, "base/command", "AmericaCommandCenter", false, 119000u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(result.hasTarget && result.objectId == 500u, "Fresh stale command structures should be valid strategic fallback targets");
		expect(!result.visible && result.stale, "Fresh stale SCUD Storm strategic target should report stale state");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.candidates.push_back(makeScudStormTarget(600u, "production", "ChinaWarFactory", false, 121000u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(!result.hasTarget, "Stale SCUD Storm strategic targets beyond freshness window should be rejected");
		expect(std::strcmp(result.reason, "no_valid_fresh_targets") == 0,
			"Expired stale SCUD Storm strategic targets should report no_valid_fresh_targets");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		AIControlAdapterScudStormStrategicTargetCandidate friendly =
			makeScudStormTarget(700u, "base/command", "ChinaCommandCenter", true, 0u);
		friendly.enemyOwned = false;
		inputs.candidates.push_back(friendly);
		AIControlAdapterScudStormStrategicTargetCandidate destroyed =
			makeScudStormTarget(701u, "production", "ChinaWarFactory", true, 0u);
		destroyed.alive = false;
		inputs.candidates.push_back(destroyed);
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(!result.hasTarget, "SCUD Storm strategic fallback should reject friendly/captured and destroyed targets");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = false;
		inputs.candidates.push_back(makeScudStormTarget(800u, "base/command", "ChinaCommandCenter", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(!result.hasTarget, "Strategic SCUD Storm fallback should hold without a ready SCUD Storm");
		expect(std::strcmp(result.reason, "no_ready_scud_storm") == 0,
			"No-ready strategic SCUD Storm fallback should report no_ready_scud_storm");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.fireCooldownActive = true;
		inputs.candidates.push_back(makeScudStormTarget(900u, "base/command", "ChinaCommandCenter", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(!result.hasTarget, "Strategic SCUD Storm fallback should respect the fire cooldown");
		expect(std::strcmp(result.reason, "cooldown") == 0,
			"Cooldown strategic SCUD Storm fallback should report cooldown");
	}

	{
		AIControlAdapterScudStormStrategicTargetInputs inputs;
		inputs.hasReadyScudStorm = true;
		inputs.candidates.push_back(makeScudStormTarget(1000u, "economy", "GLABlackMarket", true, 0u));
		const auto result = AIControlAdapterSelectScudStormStrategicTarget(inputs);
		expect(result.hasTarget, "Strategic SCUD Storm fallback should select one valid target");
		expect(result.maxFireCount == 1, "Strategic SCUD Storm fallback should conservatively fire one SCUD Storm per interval");
	}

	{
		const auto result = AIControlAdapterClassifyMatchOutcome({
			true,
			false,
			false,
			false,
			0u
		});
		expect(result.state == "running", "Match outcome should report running when no terminal flags are set");
		expect(std::strcmp(result.reason, "in_progress") == 0, "Running match outcome should report in_progress");
	}

	{
		const auto result = AIControlAdapterClassifyMatchOutcome({
			true,
			true,
			false,
			false,
			123u
		});
		expect(result.state == "victory", "Allied victory should classify as victory");
		expect(std::strcmp(result.reason, "allied_victory") == 0, "Allied victory should report allied_victory");
	}

	{
		const auto result = AIControlAdapterClassifyMatchOutcome({
			true,
			false,
			true,
			false,
			123u
		});
		expect(result.state == "defeat", "Allied defeat should classify as defeat");
		expect(std::strcmp(result.reason, "allied_defeat") == 0, "Allied defeat should report allied_defeat");
	}

	{
		const auto result = AIControlAdapterClassifyMatchOutcome({
			true,
			false,
			false,
			true,
			123u
		});
		expect(result.state == "defeat", "Local defeat should classify as defeat");
		expect(std::strcmp(result.reason, "local_defeat") == 0, "Local defeat should report local_defeat when allied defeat is absent");
	}

	{
		const auto result = AIControlAdapterClassifyMatchOutcome({
			false,
			false,
			false,
			false,
			0u
		});
		expect(result.state == "unknown", "Unavailable victory conditions should classify as unknown");
		expect(std::strcmp(result.reason, "victory_conditions_unavailable") == 0,
			"Unavailable victory conditions should report victory_conditions_unavailable");
	}

	{
		const auto result = AIControlAdapterClassifyMatchOutcome({
			true,
			false,
			false,
			false,
			999u
		});
		expect(result.state == "draw_or_unknown", "End frame without local result should classify as draw_or_unknown");
		expect(std::strcmp(result.reason, "end_frame_without_local_result") == 0,
			"Draw/unknown terminal state should explain missing local result");
	}

	{
		expect(AIControlAdapterShouldLogTerminalMatchOutcome("", "victory"),
			"First terminal match outcome should be logged");
		expect(!AIControlAdapterShouldLogTerminalMatchOutcome("victory", "victory"),
			"Repeated terminal match outcome should not be logged again");
		expect(!AIControlAdapterShouldLogTerminalMatchOutcome("", "running"),
			"Running match outcome should not emit terminal log");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			false,
			true,
			false,
			false,
			false,
			"running"
		});
		expect(std::strcmp(result.action, "cache_current_active") == 0,
			"Durable match outcome should cache meaningful running context");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			false,
			true,
			false,
			false,
			false,
			"unknown"
		});
		expect(std::strcmp(result.action, "cache_current_active") == 0,
			"Durable match outcome should cache meaningful active context even before victory conditions are available");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			false,
			false,
			true,
			false,
			true,
			"running"
		});
		expect(std::strcmp(result.action, "return_cached_terminal") == 0,
			"Durable match outcome should return cached terminal after context loss");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			false,
			true,
			true,
			false,
			true,
			"running"
		});
		expect(std::strcmp(result.action, "return_cached_terminal") == 0,
			"Durable match outcome should prefer cached terminal over rebuilding a meaningful running state");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			false,
			false,
			false,
			false,
			true,
			"running"
		});
		expect(std::strcmp(result.action, "create_lost_context_unknown") == 0,
			"Durable match outcome should create explicit unknown after context loss before terminal");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			false,
			true,
			false,
			true,
			true,
			"running"
		});
		expect(std::strcmp(result.action, "return_cached_unknown") == 0,
			"Durable match outcome should prefer durable unknown over rebuilding a meaningful running state");
	}

	{
		const auto result = AIControlAdapterChooseDurableMatchOutcomeAction({
			true,
			true,
			true,
			false,
			true,
			"running"
		});
		expect(std::strcmp(result.action, "reset_for_new_match") == 0,
			"Durable match outcome should clear cached terminal only for a new meaningful match");
	}

	{
		const auto result = AIControlAdapterClassifyMatchParticipant({
			true,
			true,
			true,
			true,
			true,
			false
		});
		expect(result.includedInOutcome, "Local player with occupied slot should remain included in match outcome");
	}

	{
		const auto result = AIControlAdapterClassifyMatchParticipant({
			false,
			true,
			false,
			false,
			false,
			false
		});
		expect(!result.includedInOutcome, "Closed/default slots should not be counted as active participants");
		expect(std::strcmp(result.nonParticipantReason, "closed_slot_or_no_assets") == 0,
			"Closed/default slots should expose a concrete non-participant reason");
	}

	{
		const auto result = AIControlAdapterClassifyMatchParticipant({
			false,
			true,
			true,
			true,
			true,
			false
		});
		expect(result.includedInOutcome, "Occupied AI opponent with valid setup should count as an active participant");
	}

	{
		expect(
			AIControlAdapterEnemyMemory::classifyTemplate("ChinaNuclearMissileLauncher", true) == EnemyMemoryKind::Wmd,
			"Enemy memory should classify nuclear missile launcher as wmd");
		expect(
			AIControlAdapterEnemyMemory::classifyTemplate("GLAScudStorm", true) == EnemyMemoryKind::Wmd,
			"Enemy memory should classify SCUD Storm as wmd");
		expect(
			AIControlAdapterEnemyMemory::classifyTemplate("ChinaWarFactory", true) == EnemyMemoryKind::Production,
			"Enemy memory should classify War Factory as production");
		expect(
			AIControlAdapterEnemyMemory::classifyTemplate("GLAArmsDealer", true) == EnemyMemoryKind::Production,
			"Enemy memory should classify Arms Dealer as production");
		expect(
			AIControlAdapterEnemyMemory::classifyTemplate("GLASupplyStash", true) == EnemyMemoryKind::Economy,
			"Enemy memory should classify Supply Stash as economy");
		expect(
			AIControlAdapterEnemyMemory::classifyTemplate("AmericaPatriotBattery", true) == EnemyMemoryKind::Defense,
			"Enemy memory should classify Patriot as defense");
	}

	{
		AIControlAdapterEnemyMemory memory;
		EnemyMemoryObservation observation;
		observation.objectId = 4123u;
		observation.playerIndex = 2;
		observation.team = 1;
		observation.templateName = "GLABarracks";
		observation.isStructure = true;
		observation.isUnit = false;
		observation.position.x = 4200.0f;
		observation.position.y = 1800.0f;
		observation.position.z = 0.0f;
		observation.seenTick = 1000u;

		memory.beginUpdate(1000u);
		memory.observe(observation);
		memory.finishUpdate(1000u);
		expect(memory.getItems().size() == 1u, "Enemy memory should store visible enemy structure");
		expect(memory.getItems()[0].visible, "Enemy memory item should be visible after observation");
		expect(!memory.getItems()[0].stale, "Enemy memory item should not be stale while visible");

		memory.beginUpdate(2500u);
		memory.finishUpdate(2500u);
		expect(memory.getItems().size() == 1u, "Enemy memory should preserve last-known structure after visibility loss");
		expect(!memory.getItems()[0].visible, "Enemy memory item should become non-visible without a new observation");
		expect(memory.getItems()[0].stale, "Enemy memory item should become stale without a new observation");
		expect(memory.buildTelemetry(2500u)["items"][0]["age_ms"].get<unsigned int>() == 1500u,
			"Enemy memory telemetry should expose stale age");
	}

	{
		AIControlAdapterEnemyMemory memory;
		EnemyMemoryObservation observation;
		observation.objectId = 81u;
		observation.playerIndex = 3;
		observation.team = -1;
		observation.templateName = "GLASneakAttackTunnelNetwork";
		observation.isStructure = true;
		observation.isUnit = false;
		observation.position.x = 231.0f;
		observation.position.y = 610.0f;
		observation.seenTick = 1000u;

		memory.beginUpdate(1000u);
		memory.observe(observation);
		memory.finishUpdate(1000u);
		expect(memory.getItems().empty(), "Enemy memory should ignore transient Sneak Attack tunnel markers");
	}

	{
		AIControlAdapterEnemyMemory memory;
		memory.beginUpdate(9000u);
		for (unsigned int i = 0; i < 3u; ++i)
		{
			EnemyMemoryObservation unit;
			unit.objectId = 5000u + i;
			unit.playerIndex = 3;
			unit.team = 2;
			unit.templateName = "ChinaTankBattleMaster";
			unit.isStructure = false;
			unit.isUnit = true;
			unit.position.x = 3000.0f + static_cast<float>(i * 40u);
			unit.position.y = 2600.0f;
			unit.seenTick = 9000u;
			memory.observe(unit);
		}
		memory.finishUpdate(9000u);
		expect(memory.getItems().empty(), "Enemy memory should not store ordinary units as individual permanent items");
		expect(memory.getClusters().size() == 1u, "Enemy memory should cluster visible enemy units");
		expect(memory.getClusters()[0].visibleCount == 3, "Enemy unit cluster should preserve visible count");
	}

	{
		const AIControlAdapterRocketBuggyMixResult result = AIControlAdapterChooseRocketBuggyMix({
			true,
			true,
			false,
			0,
			0,
			8,
			8,
			0
		});
		expect(result.desiredBuggies >= 3, "Rocket Buggies should have a baseline late-game vehicle mix target without siege detection");
		expect(result.productionNeeded, "Rocket Buggy baseline mix should request production when below target");
		expect(std::strcmp(result.reason, "late_game_mix") == 0, "Rocket Buggy baseline production should report late_game_mix");
	}

	{
		const AIControlAdapterRocketBuggyMixResult result = AIControlAdapterChooseRocketBuggyMix({
			true,
			true,
			false,
			4,
			0,
			8,
			8,
			0
		});
		expect(!result.productionNeeded, "Rocket Buggy baseline mix should stop once current buggies meet the target");
		expect(std::strcmp(result.reason, "late_game_mix") == 0, "Satisfied Rocket Buggy baseline should still report late_game_mix");
	}

	{
		const AIControlAdapterEmergencySurvivalProductionDecision result =
			AIControlAdapterChooseEmergencySurvivalProduction({
				false,
				true,
				false,
				0,
				6,
				1,
				1200u,
				10000u,
				2,
				1,
				0,
				0,
				1,
				0,
				0,
				20,
				100
			});
		expect(result.active, "Emergency survival should activate when main base is under pressure");
		expect(result.allowReserveSpend, "Emergency survival should allow combat reserve spend");
		expect(result.suppressCaptureSourceProduction, "Emergency survival should suppress capture-source production");
		expect(result.bypassArmyCapBuffer, "Emergency survival should bypass the normal army-cap buffer");
		expect(!result.commands.empty(), "Emergency survival should choose immediate combat production");
		expect(result.commands[0] == "Game.QueueQuadsAllWarFactories", "Emergency survival should prefer Quads first under pressure");
		expect(result.commands.size() >= 3u, "Emergency survival should keep Scorpions and RPGs ahead of optional Rebels");
		expect(result.commands[1] == "Game.QueueScorpionsAllWarFactories", "Emergency survival should prefer Scorpions before Barracks fallback");
		expect(result.commands[2] == "Game.QueueRpgTroopersAllBarracks", "Emergency survival should choose RPGs before Rebels");
		expect(
			std::strcmp(AIControlAdapterChooseEmergencySurvivalProductionCommand(result, 0, 0, 0, 0), "Game.QueueQuadsAllWarFactories") == 0,
			"Emergency survival should still seed the first Quad when both vehicle buckets are empty");
		expect(
			std::strcmp(AIControlAdapterChooseEmergencySurvivalProductionCommand(result, 6, 1, 1, 0), "Game.QueueScorpionsAllWarFactories") == 0,
			"Emergency survival should choose Scorpions when the army is quad-heavy");
		expect(
			std::strcmp(AIControlAdapterChooseEmergencySurvivalProductionCommand(result, 4, 0, 5, 0), "Game.QueueQuadsAllWarFactories") == 0,
			"Emergency survival should return to Quads once Scorpions catch up");
	}

	{
		const AIControlAdapterEmergencySurvivalProductionDecision result =
			AIControlAdapterChooseEmergencySurvivalProduction({
				true,
				false,
				false,
				0,
				2,
				0,
				15000u,
				10000u,
				3,
				3,
				0,
				5,
				5,
				6,
				4,
				151,
				100
			});
		expect(result.active, "Brutal pressure emergency priority should activate emergency survival production");
		expect(result.emergencyArmyCap == 150, "Emergency survival should use a bounded 150 percent army cap override");
		expect(result.commands.empty(), "Emergency survival should stop producing at the bounded emergency cap");
		expect(std::strcmp(result.reason, "emergency_cap_reached") == 0, "Emergency cap block should be explicit");
	}

	{
		const AIControlAdapterEmergencySurvivalProductionDecision result =
			AIControlAdapterChooseEmergencySurvivalProduction({
				false,
				false,
				false,
				1,
				4,
				0,
				11000u,
				10000u,
				0,
				1,
				0,
				0,
				0,
				0,
				0,
				15,
				60
			});
		expect(result.active, "High or critical zone threats with local enemies should activate emergency survival");
		expect(result.commands.size() == 1u && result.commands[0] == "Game.QueueRpgTroopersAllBarracks",
			"Emergency survival should choose RPGs before optional Rebels when only Barracks are available");
	}

	{
		const AIControlAdapterEmergencySurvivalProductionDecision result =
			AIControlAdapterChooseEmergencySurvivalProduction({
				false,
				false,
				false,
				0,
				0,
				0,
				9000u,
				10000u,
				2,
				2,
				0,
				0,
				0,
				0,
				0,
				10,
				100
			});
		expect(!result.active, "Non-emergency production should leave normal reserve behavior unchanged");
		expect(!result.allowReserveSpend, "Non-emergency production should not spend reserve");
		expect(!result.suppressCaptureSourceProduction, "Capture-source production should remain available outside emergency survival");
	}

	{
		const AIControlAdapterPalaceRecoveryDecision result = AIControlAdapterChoosePalaceRecovery({
			0,
			0,
			true,
			false,
			3000u,
			2500u,
			true
		});
		expect(result.shouldBuild, "Missing Palace under WMD threat should request Palace recovery");
		expect(std::strcmp(result.reason, "missing_palace_under_wmd_threat") == 0, "Palace recovery should report WMD recovery reason");
	}

	{
		const AIControlAdapterPalaceRecoveryDecision result = AIControlAdapterChoosePalaceRecovery({
			0,
			1,
			true,
			true,
			12000u,
			2500u,
			true
		});
		expect(!result.shouldBuild, "Healthy Palace in progress should prevent duplicate Palace recovery");
		expect(std::strcmp(result.reason, "healthy_in_progress") == 0, "Palace recovery duplicate block should be explicit");
	}

	{
		const AIControlAdapterTerrainFacts facts = AIControlAdapterBuildTerrainFacts("Maps/Death Valley/Death Valley.map", true, 1200.0f, 1200.0f);
		expect(facts.source == "manual_fixture", "Death Valley terrain facts should use a manual fixture when map identity is known");
		expect(facts.features.size() >= 4u, "Death Valley terrain facts should include barriers and base entrances");
		for (std::size_t i = 0; i < facts.features.size(); ++i)
		{
			expect(facts.features[i].id != "death-valley-main-base-lower-entry",
				"Death Valley fixture should not emit the unverified lower entry marker");
		}
		const nlohmann::json telemetry = AIControlAdapterSerializeTerrainFacts(facts);
		expect(telemetry["terrain_features"].is_array(), "Terrain telemetry should serialize features as an array");
		expect(telemetry["terrain_features"].size() == facts.features.size(), "Terrain telemetry should preserve feature count");
		expect(telemetry["terrain_features"][0]["id"].is_string(), "Terrain telemetry should include feature ids");
		expect(telemetry["terrain_features"][0]["kind"].is_string(), "Terrain telemetry should include feature kinds");
		expect(telemetry["terrain_features"][0]["source"].is_string(), "Terrain telemetry should include feature sources");
	}

	{
		const AIControlAdapterTerrainFacts facts = AIControlAdapterBuildTerrainFacts("Maps/Unknown/Unknown.map", true, 1200.0f, 1200.0f);
		const AIControlAdapterZoneTerrainResult result = AIControlAdapterApplyZoneTerrainFacts(facts, {
			1200.0f,
			1200.0f,
			700.0f,
			1.0f,
			0.0f,
			true
		});
		expect(facts.source == "unavailable", "Unknown maps should explicitly report unavailable terrain facts");
		expect(facts.features.empty(), "Unknown maps should not invent terrain features");
		expect(!result.terrainLimited, "Missing terrain facts should not change zone radius behavior");
		expect(result.effectiveRadius == 700.0f, "Missing terrain facts should preserve original zone radius");
		expect(std::strcmp(result.reason, "unavailable") == 0, "Missing terrain facts should report unavailable zone adjustment");
	}

	{
		const AIControlAdapterTerrainFacts facts = AIControlAdapterBuildTerrainFacts("DeathValley", true, 1200.0f, 1200.0f);
		const AIControlAdapterZoneTerrainResult result = AIControlAdapterApplyZoneTerrainFacts(facts, {
			1200.0f,
			1200.0f,
			1200.0f,
			1.0f,
			0.35f,
			true
		});
		expect(result.terrainLimited, "Known barriers should reduce an oversized zone radius");
		expect(result.effectiveRadius < 1200.0f, "Known barriers should produce a smaller effective radius");
		expect(result.hasEntrance, "Main-base terrain facts should select a base entrance");
		expect(!result.entranceId.empty(), "Selected terrain entrance should expose an id");
		expect(result.frontPoint.x == result.entrancePosition.x && result.frontPoint.y == result.entrancePosition.y,
			"Terrain entrance should become the zone front point");
		expect(result.rearPoint.x < 1200.0f,
			"Terrain-aware rear point should move to the safer side opposite the selected entrance");
	}

	{
		AIControlAdapterTerrainFacts facts;
		facts.mapName = "BarrierTest";
		facts.source = "unit_test";
		AIControlAdapterTerrainFeature barrier;
		barrier.id = "test-barrier";
		barrier.kind = "impassable_barrier";
		barrier.points.push_back({ 150.0f, -100.0f });
		barrier.points.push_back({ 150.0f, 100.0f });
		facts.features.push_back(barrier);
		AIControlAdapterTerrainFeature crossedEntrance;
		crossedEntrance.id = "crossed-entry";
		crossedEntrance.kind = "base_entrance";
		crossedEntrance.hasPosition = true;
		crossedEntrance.position = { 260.0f, 0.0f };
		crossedEntrance.trusted = true;
		facts.features.push_back(crossedEntrance);
		AIControlAdapterTerrainFeature sameSideEntrance;
		sameSideEntrance.id = "same-side-entry";
		sameSideEntrance.kind = "base_entrance";
		sameSideEntrance.hasPosition = true;
		sameSideEntrance.position = { 100.0f, 60.0f };
		sameSideEntrance.trusted = true;
		facts.features.push_back(sameSideEntrance);

		const AIControlAdapterZoneTerrainResult result = AIControlAdapterApplyZoneTerrainFacts(facts, {
			0.0f,
			0.0f,
			500.0f,
			1.0f,
			0.0f,
			true
		});
		expect(result.hasEntrance, "Terrain point selection should still choose an entrance when one is available");
		expect(result.entranceId == "same-side-entry",
			"Terrain point selection should avoid an entrance whose anchor path crosses a barrier");
		expect(result.terrainLimited, "Nearby barrier should still limit the effective radius");
	}

	{
		AIControlAdapterTerrainFacts extracted;
		extracted.mapName = "TestMap";
		extracted.source = "engine_sample";
		extracted.extraction.mapName = "TestMap";
		extracted.extraction.sampleStep = 128;
		extracted.extraction.cliffSamples = 3;
		extracted.extraction.passableSamples = 12;
		extracted.extraction.reason = "engine_sampling_available";
		AIControlAdapterTerrainFeature sample;
		sample.id = "cliff-sample-test";
		sample.kind = "cliff_sample";
		sample.source = "engine_sample";
		sample.hasPosition = true;
		sample.position.x = 100.0f;
		sample.position.y = 200.0f;
		extracted.features.push_back(sample);

		const AIControlAdapterTerrainFacts fixture = AIControlAdapterBuildTerrainFacts("DeathValley", true, 1200.0f, 1200.0f);
		const AIControlAdapterTerrainFacts selected = AIControlAdapterSelectTerrainFacts(extracted, fixture);
		expect(selected.source == "engine_sample", "Extracted terrain facts should win provider selection when available");
		expect(!selected.extraction.fallbackUsed, "Extracted provider should not mark fixture fallback as used");
		expect(selected.extraction.fallbackSource == "manual_fixture", "Provider selection should record available fixture fallback");
		const nlohmann::json telemetry = AIControlAdapterSerializeTerrainFacts(selected);
		expect(telemetry["terrain_source"] == "engine_sample", "Terrain telemetry should expose selected extraction source");
		expect(telemetry["terrain_extraction"]["sample_step"] == 128, "Terrain telemetry should expose sample step");
		expect(telemetry["terrain_extraction"]["cliff_samples"] == 3, "Terrain telemetry should expose cliff sample count");
		expect(telemetry["terrain_extraction"]["fallback_used"] == false, "Terrain telemetry should expose fallback usage");
	}

	{
		AIControlAdapterTerrainFacts extracted;
		extracted.mapName = "DeathValley";
		extracted.source = "unavailable";
		extracted.extraction.mapName = "DeathValley";
		extracted.extraction.reason = "engine_probe_no_features";
		const AIControlAdapterTerrainFacts fixture = AIControlAdapterBuildTerrainFacts("DeathValley", true, 1200.0f, 1200.0f);
		const AIControlAdapterTerrainFacts selected = AIControlAdapterSelectTerrainFacts(extracted, fixture);
		expect(selected.source == "manual_fixture", "Manual fixture should be used when extraction is unavailable");
		expect(selected.extraction.fallbackUsed, "Fixture selection should report fallback usage");
		expect(selected.extraction.fallbackSource == "unavailable", "Fixture fallback should record unavailable extraction source");
	}

	{
		AIControlAdapterTerrainFacts extracted;
		extracted.mapName = "Unknown";
		extracted.source = "unavailable";
		extracted.extraction.mapName = "Unknown";
		const AIControlAdapterTerrainFacts fixture;
		const AIControlAdapterTerrainFacts selected = AIControlAdapterSelectTerrainFacts(extracted, fixture);
		expect(selected.source == "unavailable", "Terrain provider should remain unavailable without extraction or fixture");
		expect(selected.features.empty(), "Unavailable terrain provider should not add features");
	}

	{
		expect(AIControlAdapterNormalizeMapFileCacheKey("Maps\\Death Valley\\Death Valley.map") == "death-valley",
			"Map cache normalization should handle Windows map paths");
		expect(AIControlAdapterNormalizeMapFileCacheKey("DeathValley.map") == "death-valley",
			"Map cache normalization should split compact camel-case launch names");
		expect(AIControlAdapterNormalizeMapFileCacheKey("  Death_Valley!.map") == "death-valley",
			"Map cache normalization should collapse punctuation to hyphens");
	}

	{
		nlohmann::json cache = nlohmann::json::object({
			{"schema_version", 1},
			{"map", nlohmann::json::object({ {"name", "Death Valley"}, {"hash", "abc123"} })},
			{"metadata", nlohmann::json::object({
				{"decoded_waypoint_count", 1},
				{"decoded_waypoint_link_count", 1},
				{"decoded_cliff_cell_count", 12},
				{"derived_barrier_feature_count", 1}
			})},
			{"features", nlohmann::json::array({
				nlohmann::json::object({
					{"id", "waypoint-1"},
					{"kind", "waypoint"},
					{"source", "map_file"},
					{"position", nlohmann::json::object({ {"x", 100.0}, {"y", 200.0} })},
					{"labels", nlohmann::json::array({ "Center1" })}
				}),
				nlohmann::json::object({
					{"id", "waypoint-lane-1-2"},
					{"kind", "lane"},
					{"source", "map_file"},
					{"points", nlohmann::json::array({
						nlohmann::json::object({ {"x", 100.0}, {"y", 200.0} }),
						nlohmann::json::object({ {"x", 300.0}, {"y", 400.0} })
					})},
					{"labels", nlohmann::json::array({ "Center1" })}
				}),
				nlohmann::json::object({
					{"id", "terrain-barrier-1"},
					{"kind", "impassable_barrier"},
					{"source", "map_file_terrain"},
					{"points", nlohmann::json::array({
						nlohmann::json::object({ {"x", 500.0}, {"y", 1200.0} }),
						nlohmann::json::object({ {"x", 1500.0}, {"y", 1200.0} })
					})},
					{"width", 300.0}
				}),
				nlohmann::json::object({
					{"id", "pending-entry"},
					{"kind", "base_entrance"},
					{"source", "manual_annotation"},
					{"position", nlohmann::json::object({ {"x", 1.0}, {"y", 2.0} })}
				}),
				nlohmann::json::object({
					{"id", "trusted-entry"},
					{"kind", "base_entrance"},
					{"source", "manual_annotation"},
					{"trusted", true},
					{"position", nlohmann::json::object({ {"x", 900.0}, {"y", 4200.0} })},
					{"width", 450.0},
					{"connects", nlohmann::json::array({ "main_base", "lower_approach" })}
				})
			})},
			{"objects", nlohmann::json::array({
				nlohmann::json::object({
					{"id", "SupplyDock 1"},
					{"kind", "economy"},
					{"template", "SupplyDock"},
					{"position", nlohmann::json::object({ {"x", 500.0}, {"y", 600.0} })}
				})
			})},
			{"warnings", nlohmann::json::array()}
		});

		AIControlAdapterTerrainFacts facts;
		std::string reason;
		expect(AIControlAdapterParseMapFileCacheJson(cache, "Death Valley.map", "death-valley.json", facts, reason),
			"Valid map-file cache fixture should parse");
		expect(facts.source == "map_file_cache", "Parsed cache should identify map_file_cache as source");
		expect(facts.features.size() == 4, "Parsed cache should include waypoint, lane, terrain barrier, and trusted entrance");
		expect(facts.strategicObjects.size() == 1, "Parsed cache should expose strategic objects");
		expect(facts.mapFileCacheTelemetry["loaded"] == true, "Parsed cache telemetry should mark loaded");
		expect(facts.mapFileCacheTelemetry["ignored_semantic_features"] == 1, "Pending base entrance cache features should be ignored");
		expect(facts.mapFileCacheTelemetry["annotation_features"] == 2, "Parsed cache telemetry should count annotation features");
		expect(facts.mapFileCacheTelemetry["trusted_entrances"] == 1, "Parsed cache telemetry should count trusted entrances");
		expect(facts.mapFileCacheTelemetry["semantic_features_loaded"] == 1, "Parsed cache telemetry should count loaded semantic features");
		expect(facts.mapFileCacheTelemetry["derived_barriers"] == 1, "Parsed cache telemetry should expose derived barrier count");
		expect(facts.features[0].source == "map_file_cache", "Parsed cache features should be tagged map_file_cache");
		expect(facts.features[0].connects.size() == 1 && facts.features[0].connects[0] == "Center1",
			"Parsed cache labels should be retained for UI/telemetry");
		expect(facts.features[2].kind == "impassable_barrier" && facts.features[2].width == 300.0f,
			"Parsed cache should expose generated terrain barriers with width");
		expect(facts.features[3].kind == "base_entrance" && facts.features[3].trusted,
			"Parsed cache should accept trusted annotation entrances");
		expect(facts.features[3].connects.size() == 2 && facts.features[3].connects[1] == "lower_approach",
			"Parsed cache should retain annotation entrance connects");
	}

	{
		AIControlAdapterTerrainFacts facts;
		std::string reason;
		expect(!AIControlAdapterParseMapFileCacheJson(nlohmann::json::object({ {"schema_version", 99} }),
			"Bad.map", "bad.json", facts, reason), "Unsupported cache schema should fail cleanly");
		expect(reason == "unsupported_schema", "Unsupported cache schema should report unsupported_schema");
	}

	{
		AIControlAdapterTerrainFacts extracted;
		extracted.mapName = "DeathValley";
		extracted.source = "engine_query";
		extracted.extraction.mapName = "DeathValley";
		AIControlAdapterTerrainFeature runtimeLane;
		runtimeLane.id = "waypoint-lane-1-2";
		runtimeLane.kind = "lane";
		runtimeLane.source = "engine_query";
		runtimeLane.points.push_back({ 100.0f, 200.0f });
		runtimeLane.points.push_back({ 300.0f, 400.0f });
		extracted.features.push_back(runtimeLane);

		AIControlAdapterTerrainFacts cacheFacts;
		cacheFacts.mapName = "DeathValley";
		cacheFacts.source = "map_file_cache";
		cacheFacts.mapFileCacheTelemetry = nlohmann::json::object({ {"loaded", true}, {"reason", "matched_normalized_map_name"} });
		cacheFacts.features.push_back(runtimeLane);
		cacheFacts.features.back().source = "map_file_cache";
		AIControlAdapterTerrainFeature cacheWaypoint;
		cacheWaypoint.id = "waypoint-1";
		cacheWaypoint.kind = "waypoint";
		cacheWaypoint.source = "map_file_cache";
		cacheWaypoint.hasPosition = true;
		cacheWaypoint.position.x = 100.0f;
		cacheWaypoint.position.y = 200.0f;
		cacheFacts.features.push_back(cacheWaypoint);

		const AIControlAdapterTerrainFacts fixture = AIControlAdapterBuildTerrainFacts("DeathValley", true, 1200.0f, 1200.0f);
		const AIControlAdapterTerrainFacts merged = AIControlAdapterMergeMapFileCacheFacts(extracted, fixture, cacheFacts);
		expect(merged.source == "engine_query", "Runtime terrain source should remain selected when available");
		expect(merged.features.size() > extracted.features.size(), "Cache waypoint and fixture semantics should supplement runtime facts");
		int runtimeLaneCount = 0;
		int cacheWaypointCount = 0;
		int entranceCount = 0;
		for (std::size_t i = 0; i < merged.features.size(); ++i)
		{
			if (merged.features[i].id == "waypoint-lane-1-2")
			{
				++runtimeLaneCount;
			}
			if (merged.features[i].id == "waypoint-1" && merged.features[i].source == "map_file_cache")
			{
				++cacheWaypointCount;
			}
			if (merged.features[i].kind == "base_entrance")
			{
				++entranceCount;
			}
		}
		expect(runtimeLaneCount == 1, "Cache merge should not duplicate runtime lane ids");
		expect(cacheWaypointCount == 1, "Cache merge should add non-duplicate waypoint features");
		expect(entranceCount > 0, "Fixture entrance semantics should remain present after cache merge");
		const nlohmann::json telemetry = AIControlAdapterSerializeTerrainFacts(merged);
		expect(telemetry["map_file_cache"]["loaded"] == true, "Merged telemetry should expose loaded map-file cache");
	}

	{
		AIControlAdapterTerrainFacts extracted;
		extracted.mapName = "DeathValley";
		extracted.source = "engine_query";
		extracted.extraction.mapName = "DeathValley";
		AIControlAdapterTerrainFeature runtimeLane;
		runtimeLane.id = "waypoint-lane-1-2";
		runtimeLane.kind = "lane";
		runtimeLane.source = "engine_query";
		runtimeLane.points.push_back({ 100.0f, 200.0f });
		runtimeLane.points.push_back({ 300.0f, 400.0f });
		extracted.features.push_back(runtimeLane);

		AIControlAdapterTerrainFacts cacheFacts;
		cacheFacts.mapName = "DeathValley";
		cacheFacts.source = "map_file_cache";
		cacheFacts.mapFileCacheTelemetry = nlohmann::json::object({
			{"loaded", true},
			{"trusted_entrances", 1},
			{"reason", "matched_normalized_map_name"}
		});
		AIControlAdapterTerrainFeature trustedEntry;
		trustedEntry.id = "death-valley-main-base-lower-entry";
		trustedEntry.kind = "base_entrance";
		trustedEntry.source = "map_file_cache";
		trustedEntry.trusted = true;
		trustedEntry.hasPosition = true;
		trustedEntry.position.x = 900.0f;
		trustedEntry.position.y = 4200.0f;
		trustedEntry.width = 450.0f;
		cacheFacts.features.push_back(trustedEntry);

		const AIControlAdapterTerrainFacts fixture = AIControlAdapterBuildTerrainFacts("DeathValley", true, 1200.0f, 1200.0f);
		const AIControlAdapterTerrainFacts merged = AIControlAdapterMergeMapFileCacheFacts(extracted, fixture, cacheFacts);
		int fixtureEntrances = 0;
		bool foundTrustedEntry = false;
		for (std::size_t i = 0; i < merged.features.size(); ++i)
		{
			if (merged.features[i].kind == "base_entrance" && merged.features[i].source == "manual_fixture")
			{
				++fixtureEntrances;
			}
			if (merged.features[i].id == "death-valley-main-base-lower-entry"
				&& merged.features[i].source == "map_file_cache"
				&& merged.features[i].trusted
				&& merged.features[i].hasPosition
				&& merged.features[i].position.x == 900.0f)
			{
				foundTrustedEntry = true;
			}
		}
		expect(!foundTrustedEntry, "Unverified lower Death Valley cache entrance should be ignored even if marked trusted");
		expect(fixtureEntrances > 0, "Ignored lower cache entrance should not demote safer fixture entrances");
		expect(merged.mapFileCacheTelemetry["ignored_lower_entry"] == true,
			"Ignored lower cache entrance should report telemetry");
	}

	{
		const std::vector<std::string> hints = AIControlAdapterChooseRunDiagnosisHints({
			"depleted",
			0,
			0,
			0,
			2,
			1,
			1,
			0
		});
		expect(std::find(hints.begin(), hints.end(), "economy_reserve_depleted") != hints.end(),
			"Final run hints should include reserve depletion only when supported");
		expect(std::find(hints.begin(), hints.end(), "no_command_center") != hints.end(),
			"Final run hints should include missing command center");
		expect(std::find(hints.begin(), hints.end(), "no_workers") != hints.end(),
			"Final run hints should include no workers");
		expect(std::find(hints.begin(), hints.end(), "no_producers") != hints.end(),
			"Final run hints should include no producers");
		expect(std::find(hints.begin(), hints.end(), "zone_reserve_deficits") != hints.end(),
			"Final run hints should include reserve deficits");
		expect(std::find(hints.begin(), hints.end(), "stalled_construction_tasks") != hints.end(),
			"Final run hints should include stalled construction");
		expect(std::find(hints.begin(), hints.end(), "enemy_wmd_still_known") != hints.end(),
			"Final run hints should include known enemy WMD");
		expect(std::find(hints.begin(), hints.end(), "attack_waves_inactive") != hints.end(),
			"Final run hints should include inactive attack waves");
	}

	{
		const std::vector<std::string> hints = AIControlAdapterChooseRunDiagnosisHints({
			"protected",
			1,
			8,
			3,
			0,
			0,
			0,
			1
		});
		expect(hints.empty(), "Final run hints should not emit unsupported diagnoses");
	}

	{
		const AIControlAdapterGarrisonCandidateDecision palace = AIControlAdapterEvaluateGarrisonCandidate({
			true,
			false,
			true,
			false,
			false,
			true,
			false,
			true,
			false,
			false,
			false,
			120.0f,
			700.0f,
			0
		});
		expect(palace.selected, "Friendly Palace should be selected as a garrison anchor");
		expect(palace.desiredInfantry == 4, "Palace desired garrison should be 4 infantry");
		expect(palace.reason == std::string("selected"), "Palace selected reason should be selected");
	}

	{
		const AIControlAdapterGarrisonCandidateDecision nearTower = AIControlAdapterEvaluateGarrisonCandidate({
			false,
			true,
			false,
			true,
			false,
			true,
			true,
			false,
			true,
			true,
			true,
			180.0f,
			700.0f,
			0
		});
		const AIControlAdapterGarrisonCandidateDecision distantTower = AIControlAdapterEvaluateGarrisonCandidate({
			false,
			true,
			false,
			true,
			false,
			true,
			true,
			false,
			true,
			true,
			true,
			1200.0f,
			700.0f,
			0
		});
		expect(nearTower.selected, "Useful neutral map garrison near a zone should be selected");
		expect(nearTower.desiredInfantry == 8, "Useful map garrison desired count should be 8");
		expect(!distantTower.selected, "Distant map garrison should not be selected");
		expect(distantTower.reason == std::string("too_far"), "Distant map garrison should report too_far");
	}

	{
		const AIControlAdapterGarrisonCandidateDecision enemyTower = AIControlAdapterEvaluateGarrisonCandidate({
			false,
			true,
			false,
			false,
			true,
			true,
			true,
			false,
			false,
			true,
			false,
			100.0f,
			700.0f,
			0
		});
		expect(!enemyTower.selected, "Enemy-owned garrison structure should be rejected");
		expect(enemyTower.reason == std::string("enemy_owned"), "Enemy-owned garrison rejection should be explicit");
	}

	{
		const AIControlAdapterGarrisonCandidateDecision containOnly = AIControlAdapterEvaluateGarrisonCandidate({
			false,
			false,
			false,
			true,
			false,
			true,
			true,
			false,
			true,
			false,
			true,
			160.0f,
			700.0f,
			0,
			true,
			false
		});
		expect(containOnly.selected, "Runtime garrisonable civilian structure should be selected without legacy kind flags");
		expect(containOnly.desiredInfantry == 8, "Runtime garrisonable structure should use map-garrison desired count near a front");
		expect(containOnly.reason == std::string("selected"), "Runtime garrisonable selected reason should be selected");
	}

	{
		const AIControlAdapterGarrisonCandidateDecision mapCacheOnly = AIControlAdapterEvaluateGarrisonCandidate({
			false,
			false,
			false,
			true,
			false,
			true,
			false,
			true,
			false,
			false,
			false,
			220.0f,
			700.0f,
			0,
			false,
			true
		});
		expect(mapCacheOnly.selected, "Map-cache-confirmed garrison should be selected even when runtime kind flags are incomplete");
		expect(mapCacheOnly.desiredInfantry == 4, "Quiet map-cache garrison should use compact desired count");
		expect(mapCacheOnly.reason == std::string("selected"), "Map-cache garrison selected reason should be selected");
	}

	{
		const AIControlAdapterGarrisonCandidateDecision ordinaryCivilian = AIControlAdapterEvaluateGarrisonCandidate({
			false,
			false,
			false,
			true,
			false,
			true,
			true,
			false,
			false,
			false,
			false,
			100.0f,
			700.0f,
			0,
			false,
			false
		});
		expect(!ordinaryCivilian.selected, "Ordinary non-enterable civilian structure should be rejected");
		expect(ordinaryCivilian.reason == std::string("not_garrisonable"), "Non-enterable civilian rejection should be explicit");
	}

	{
		const AIControlAdapterGarrisonProductionDecision blocked = AIControlAdapterChooseGarrisonProduction({
			8,
			2,
			0,
			0,
			true,
			10200u,
			10000u
		});
		const AIControlAdapterGarrisonProductionDecision allowed = AIControlAdapterChooseGarrisonProduction({
			8,
			2,
			0,
			0,
			true,
			12000u,
			10000u
		});
		expect(!blocked.productionNeeded, "Garrison production should protect reserve plus unit cost");
		expect(blocked.reason == std::string("reserve_protected"), "Reserve-protected garrison production should explain blocker");
		expect(allowed.productionNeeded, "Garrison production should request RPG infantry when useful and affordable");
		expect(allowed.desiredQueued == 2, "Garrison production should be bounded to two queued units per decision");
	}

	{
		const AIControlAdapterGarrisonThroughputDecision normal = AIControlAdapterEvaluateGarrisonThroughput({
			8000u,
			5000u,
			2,
			3,
			0,
			20,
			4,
			2,
			10,
			2,
			0,
			0,
			0,
			false
		});
		expect(normal.maxAssignmentsThisCycle == 1, "Normal garrison throughput should remain conservative");
		expect(normal.mode == std::string("normal"), "Normal throughput mode should be explicit");
	}

	{
		const AIControlAdapterGarrisonThroughputDecision accelerated = AIControlAdapterEvaluateGarrisonThroughput({
			300000u,
			10000u,
			22,
			34,
			16,
			172,
			89,
			72,
			42,
			4,
			2,
			12,
			8,
			false
		});
		expect(accelerated.maxAssignmentsThisCycle > 1, "Late-game high-gap garrison throughput should allow multiple assignments");
		expect(accelerated.maxAssignmentsThisCycle <= 6, "Late-game garrison throughput should stay bounded");
		expect(accelerated.productionFanout >= 2, "Late-game high-gap garrisons should allow multi-barracks production fanout");
		expect(accelerated.mode == std::string("accelerated"), "Late-game throughput mode should be accelerated");
	}

	{
		const AIControlAdapterGarrisonThroughputDecision reserveHeld = AIControlAdapterEvaluateGarrisonThroughput({
			300000u,
			10000u,
			12,
			12,
			1,
			90,
			20,
			10,
			5,
			4,
			8,
			20,
			20,
			false
		});
		expect(reserveHeld.maxAssignmentsThisCycle == 0, "Garrison throughput should preserve scout, raid, and defense reserves");
		expect(reserveHeld.reason == std::string("infantry_reserve_held"), "Reserve-held throughput should explain blocker");
		expect(reserveHeld.productionFanout > 0, "High-gap reserve-held state should request bounded production fanout");
	}

	{
		const AIControlAdapterGarrisonThroughputDecision emergency = AIControlAdapterEvaluateGarrisonThroughput({
			300000u,
			10000u,
			22,
			20,
			2,
			120,
			40,
			20,
			40,
			4,
			0,
			0,
			0,
			true
		});
		expect(emergency.maxAssignmentsThisCycle == 0, "Critical emergency should hold garrison throughput");
		expect(emergency.reason == std::string("critical_emergency_reserve"), "Emergency hold reason should be explicit");
	}

	{
		const AIControlAdapterMacroBuildIntentChoice choice = AIControlAdapterChooseMacroBuildIntent({
			{ "market_growth", "Game.BuildBlackMarketSmart", 40, true, "market_growth" },
			{ "expansion", "Game.BuildSupplyStashSmart", 100, true, "coverage_gap_high_cash" },
			{ "zone_seed", "Game.BuildTunnelNetwork", 80, true, "needs_tunnel" }
		});
		expect(choice.index == 1, "Macro build intent should choose the highest-priority valid intent");
		expect(choice.category == std::string("expansion"), "Macro build intent should expose selected category");
		expect(choice.command == std::string("Game.BuildSupplyStashSmart"), "Macro build intent should expose selected command");
		expect(choice.reason == std::string("coverage_gap_high_cash"), "Macro build intent should preserve selected reason");
	}

	{
		const AIControlAdapterMacroBuildIntentChoice choice = AIControlAdapterChooseMacroBuildIntent({
			{ "expansion", "Game.BuildSupplyStashSmart", 100, false, "reserve_protected" },
			{ "zone_seed", "Game.BuildStingerSite", 70, true, "needs_stinger" },
			{ "market_growth", "Game.BuildBlackMarketSmart", 40, true, "market_growth" }
		});
		expect(choice.index == 1, "Macro build intent should skip blocked higher-priority intents");
		expect(choice.command == std::string("Game.BuildStingerSite"), "Macro build intent should fall through to the next valid candidate");
	}

	{
		const AIControlAdapterMacroBuildIntentChoice choice = AIControlAdapterChooseMacroBuildIntent({
			{ "expansion", "Game.BuildSupplyStashSmart", 100, false, "cooldown" },
			{ "market_growth", "Game.BuildBlackMarketSmart", 40, false, "reserve_protected" }
		});
		expect(choice.index == -1, "Macro build intent should return no selection when every candidate is blocked");
		expect(choice.reason == std::string("no_valid_intent"), "Macro build intent should explain empty selections");
	}

	std::cout << "AIControlAdapterPolicyTests passed\n";
	return 0;
}
