#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameClient/AIControlAdapter/AIControlAdapterEnemyMemory.h"

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
}

int main()
{
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
		100
	}) == 100, "balanced sprawl should keep the normal army cap below surplus-cash pressure");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		100000u,
		93000,
		20,
		42,
		100
	}) == 286, "balanced sprawl should scale army cap with production capacity under surplus-cash pressure");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		100000u,
		10000,
		20,
		42,
		100
	}) == 120, "balanced sprawl should limit surplus army cap when net income is modest");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		100000u,
		-1000,
		20,
		42,
		100
	}) == 100, "balanced sprawl should not raise army cap while net cash flow is negative");
	expect(AIControlAdapterGetEffectiveArmyCap({
		true,
		500000u,
		200000,
		100,
		100,
		100
	}) == 300, "balanced sprawl surplus army cap should stay bounded");
	expect(AIControlAdapterGetEffectiveArmyCap({
		false,
		500000u,
		0,
		20,
		42,
		9999
	}) == 9999, "non-balanced profiles should keep their configured army cap");

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

	std::cout << "AIControlAdapterPolicyTests passed\n";
	return 0;
}
