#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <cmath>
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

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			false,
			true,
			30000u,
			10000u,
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
			0
		};
		expect(!AIControlAdapterCanAttemptBlackMarket(inputs), "balanced sprawl should not start a black market below reserve plus cost");
	}

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			true,
			true,
			12500u,
			10000u,
			0
		};
		expect(AIControlAdapterCanAttemptBlackMarket(inputs), "balanced sprawl should allow a black market at reserve plus cost");
	}

	{
		const AIControlAdapterBlackMarketPolicyInputs inputs = {
			true,
			true,
			20000u,
			10000u,
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
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("producer_cannot_make_upgrade"), "producer_cannot_make_upgrade should stop upgrade attempts for the current tick");
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("palace_not_found"), "palace_not_found should stop upgrade attempts for the current tick");
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("black_market_not_found"), "black_market_not_found should stop upgrade attempts for the current tick");
	expect(AIControlAdapterShouldAbortUpgradePlanForTick("upgrade_already_in_production"), "upgrade_already_in_production should stop upgrade attempts for the current tick");
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
				6,
				0,
				4,
			0,
			0,
			0,
			10,
			100
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
				12,
				10,
				18,
				14,
				0,
				3,
				57,
				100
			});
			expect(std::string(result.command) == "Game.QueueSoldiersAllBarracks", "balanced sprawl should force Rebel-capable infantry when capture upgrade is ready but no capture sources exist");
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

	std::cout << "AIControlAdapterPolicyTests passed\n";
	return 0;
}
