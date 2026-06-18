/**
 * AIControlAdapterEconomyManagerTests.cpp
 *
 * Tests for economy recovery and income scaling decision logic.
 */

#include "GameClient/AIControlAdapter/AIControlAdapterEconomyManager.h"
#include <cassert>
#include <cstdio>

// Test: Healthy income and protected reserve = Normal spending mode
static void TestHealthyEconomy()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 5000u;
	input.smoothedNetCashPerMinute = 800;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 2;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Healthy);
	assert(policy.reserveState == EconomyReserveState::Protected);
	assert(policy.combatSpendingMode == CombatSpendingMode::Normal);
	assert(policy.prioritizeIncomeBuild == false);
	assert(policy.allowEmergencyIncomeBuild == false);
	assert(policy.reason == "income_sufficient");

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "income_sufficient");

	printf("PASS: TestHealthyEconomy\n");
}

// Test: Weak income + protected reserve = Conservative spending
static void TestWeakIncomeProtectedReserve()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 5000u;
	input.smoothedNetCashPerMinute = 400; // Below target (600)
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 1;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Weak);
	assert(policy.reserveState == EconomyReserveState::Protected);
	assert(policy.combatSpendingMode == CombatSpendingMode::Conservative);
	assert(policy.prioritizeIncomeBuild == false); // Reserve is protected, not emergency
	assert(policy.allowEmergencyIncomeBuild == false);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false); // No emergency when reserve is protected
	assert(request.reason == "income_sufficient"); // Actually healthy enough to not trigger

	printf("PASS: TestWeakIncomeProtectedReserve\n");
}

// Test: Critical income + pressured reserve = BlockedExceptDefense
static void TestCriticalIncomePressuredReserve()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3500u; // Near reserve
	input.smoothedNetCashPerMinute = -100; // Negative income
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Critical);
	assert(policy.reserveState == EconomyReserveState::Pressured);
	assert(policy.combatSpendingMode == CombatSpendingMode::BlockedExceptDefense);
	assert(policy.prioritizeIncomeBuild == true);
	assert(policy.allowEmergencyIncomeBuild == true);
	assert(policy.reason == "reserve_pressured_income_critical");

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == true);
	assert(request.buildingCommand == "Game.BuildBlackMarketSmart");
	assert(request.buildingTemplate == "GLABlackMarket");
	assert(request.reason == "reserve_recovery_income_build");

	printf("PASS: TestCriticalIncomePressuredReserve\n");
}

// Test: Critical income + depleted reserve = BlockedExceptDefense
static void TestCriticalIncomeDepletedReserve()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 2500u; // Below reserve
	input.smoothedNetCashPerMinute = 0; // Zero income
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Critical);
	assert(policy.reserveState == EconomyReserveState::Depleted);
	assert(policy.combatSpendingMode == CombatSpendingMode::BlockedExceptDefense);
	assert(policy.prioritizeIncomeBuild == true);
	assert(policy.allowEmergencyIncomeBuild == true);
	assert(policy.reason == "reserve_depleted_income_critical");

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == true);
	assert(request.reason == "reserve_recovery_income_build");

	printf("PASS: TestCriticalIncomeDepletedReserve\n");
}

// Test: Missing Palace prerequisite blocks Black Market request
static void TestMissingPalace()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3500u;
	input.smoothedNetCashPerMinute = -100;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = false; // Missing prerequisite
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Critical);
	assert(policy.combatSpendingMode == CombatSpendingMode::BlockedExceptDefense);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "missing_palace");

	printf("PASS: TestMissingPalace\n");
}

// Test: Black Market cap reached blocks further requests
static void TestBlackMarketCapReached()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3500u;
	input.smoothedNetCashPerMinute = -100;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 30;
	input.inProgressBlackMarkets = 2;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Max is 32 for sprawl (Phase 5.6 high hard cap), total = 30 + 2 = 32
	assert(policy.maxBlackMarkets == 32u);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "black_market_cap_reached");

	printf("PASS: TestBlackMarketCapReached\n");
}

// Test: In-progress Black Markets count toward cap and trigger Recovering state
static void TestInProgressBlackMarkets()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3500u;
	input.smoothedNetCashPerMinute = -100; // Still negative
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 1;
	input.inProgressBlackMarkets = 1; // Building
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Recovering);
	assert(policy.combatSpendingMode == CombatSpendingMode::Conservative);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	// In-progress limit reached (1 in progress, limit is 1 during recovery)
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "in_progress_limit_reached");

	printf("PASS: TestInProgressBlackMarkets\n");
}

// Test: No money blocks Black Market request
static void TestNoMoney()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 1000u; // Below Black Market cost (1500)
	input.smoothedNetCashPerMinute = -100;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Critical);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "no_money");

	printf("PASS: TestNoMoney\n");
}

// Test: Sufficient income (after recovery) stops further requests
static void TestSufficientIncomeStopsRecovery()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 5000u;
	input.smoothedNetCashPerMinute = 700; // Above sufficient (600 for sprawl)
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 2;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Healthy);
	assert(policy.combatSpendingMode == CombatSpendingMode::Normal);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "income_sufficient");

	printf("PASS: TestSufficientIncomeStopsRecovery\n");
}

// Test: Weak income requests gradual Black Market expansion
static void TestWeakIncomeGradualExpansion()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 4000u;
	input.smoothedNetCashPerMinute = 500; // Weak but positive
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Weak);
	assert(policy.desiredBlackMarkets == 1u); // Request first market

	// But since reserve is protected, no emergency
	assert(policy.prioritizeIncomeBuild == false);

	printf("PASS: TestWeakIncomeGradualExpansion\n");
}

// Test: Tech profile has lower caps and targets
static void TestTechProfileLowerCaps()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 5000u;
	input.smoothedNetCashPerMinute = 800;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "tech";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.maxBlackMarkets == 16u); // Lower than sprawl (32) but still high for scaling
	assert(policy.targetIncomePerMinute == 600u); // Lower than sprawl (800)
	assert(policy.baselineScalingMarkets < policy.maxBlackMarkets); // Baseline should be well below hard cap

	printf("PASS: TestTechProfileLowerCaps\n");
}

// Test: Emergency income build allowed when reserve pressured
static void TestEmergencyBuildAllowedWhenReservePressured()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3200u; // Pressured (< reserve + 1000)
	input.smoothedNetCashPerMinute = 200; // Weak income
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	assert(policy.incomeState == EconomyIncomeState::Weak);
	assert(policy.reserveState == EconomyReserveState::Pressured);
	assert(policy.allowEmergencyIncomeBuild == true); // Can spend reserve for recovery
	assert(policy.prioritizeIncomeBuild == true);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == true);
	assert(request.reason == "income_expansion");

	printf("PASS: TestEmergencyBuildAllowedWhenReservePressured\n");
}

// Test: Burst income + depleted reserve does not stop recovery
static void TestBurstIncomeDepletedReserve()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 2500u; // Below reserve (depleted)
	input.smoothedNetCashPerMinute = 900; // Temporarily high (e.g., stash workers returning)
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 0; // No income buildings yet
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Even though current income is high, should stay in Weak (not Healthy)
	// because reserve is depleted and we have no Black Markets
	assert(policy.incomeState == EconomyIncomeState::Weak);
	assert(policy.reserveState == EconomyReserveState::Depleted);
	assert(policy.combatSpendingMode == CombatSpendingMode::Conservative);

	printf("PASS: TestBurstIncomeDepletedReserve\n");
}

// Test: Recovering state held while reserve is depleted
static void TestRecoveringHeldWhileReserveDepleted()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 2800u; // Below reserve (depleted)
	input.smoothedNetCashPerMinute = 700; // Temporarily high
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 1;
	input.inProgressBlackMarkets = 1; // Recovery in progress
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should stay in Recovering (not Healthy) while reserve is depleted
	assert(policy.incomeState == EconomyIncomeState::Recovering);
	assert(policy.reserveState == EconomyReserveState::Depleted);
	assert(policy.combatSpendingMode == CombatSpendingMode::Conservative);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false); // Already has in-progress
	assert(request.reason == "in_progress_limit_reached");

	printf("PASS: TestRecoveringHeldWhileReserveDepleted\n");
}

// Test: High income + few markets + pressured reserve = Weak (possible burst)
static void TestPossibleBurstPressuredReserve()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3500u; // Pressured (< reserve + 1000)
	input.smoothedNetCashPerMinute = 850; // Very high for only 1 market
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 1; // Expected income = 225/min, actual = 850 (likely burst)
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should stay in Weak (not Healthy) because high income with few markets suggests burst
	assert(policy.incomeState == EconomyIncomeState::Weak);
	assert(policy.reserveState == EconomyReserveState::Pressured);
	assert(policy.combatSpendingMode == CombatSpendingMode::Conservative);

	printf("PASS: TestPossibleBurstPressuredReserve\n");
}

// Phase 5.6 Tests: Continuous Economy Scaling

// Test: Sprawl profile has high hard cap (24-40 range)
static void TestSprawlHighHardCap()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 10000u;
	input.smoothedNetCashPerMinute = 1000;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 10;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Hard cap should be high enough for continuous scaling
	assert(policy.maxBlackMarkets >= 24u);
	assert(policy.maxBlackMarkets <= 40u);

	printf("PASS: TestSprawlHighHardCap\n");
}

// Test: Growth continues at normal priority after reserve is protected
static void TestGrowthContinuesAfterReserveProtected()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 8000u; // Well above reserve (protected)
	input.smoothedNetCashPerMinute = 800;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 6; // Above soft recovery, below baseline
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should be in Growth mode, not Recovery or Saturated
	assert(policy.scalingMode == EconomyScalingMode::Growth);
	assert(policy.reserveState == EconomyReserveState::Protected);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);

	// Should continue requesting markets at normal priority
	assert(request.shouldBuildIncome == true);
	assert(request.reason == "growth_normal_priority_baseline" || request.reason == "growth_normal_priority");

	printf("PASS: TestGrowthContinuesAfterReserveProtected\n");
}

// Test: Depleted reserve triggers high-priority market growth
static void TestDepletedReserveTriggersHighPriority()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 2500u; // Below reserve (depleted)
	input.smoothedNetCashPerMinute = 200;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 2;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should be in Recovery mode with high priority
	assert(policy.scalingMode == EconomyScalingMode::Recovery);
	assert(policy.reserveState == EconomyReserveState::Depleted);
	assert(policy.prioritizeIncomeBuild == true);
	assert(policy.allowEmergencyIncomeBuild == true);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == true);
	assert(request.reason == "recovery_high_priority_reserve_critical" || request.reason == "recovery_high_priority");

	printf("PASS: TestDepletedReserveTriggersHighPriority\n");
}

// Test: Positive short-term income with low durable income does not stop scaling
static void TestShortTermIncomeDoesNotStopScaling()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 5000u;
	input.smoothedNetCashPerMinute = 900; // High short-term (supply burst)
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 2; // Low durable income (only 450/min from markets)
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should have positive economy pressure despite high current income
	assert(policy.economyPressure > 10);
	assert(policy.scalingMode == EconomyScalingMode::Growth);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);

	// Should continue scaling because durable income is low
	assert(request.shouldBuildIncome == true);

	printf("PASS: TestShortTermIncomeDoesNotStopScaling\n");
}

// Test: In-progress concurrency limits duplicate requests
static void TestInProgressConcurrencyLimit()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 3500u; // Pressured but has money
	input.smoothedNetCashPerMinute = 200;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 2;
	input.inProgressBlackMarkets = 1; // One already building
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// maxInProgressMarkets should be 1 during reserve pressure
	assert(policy.maxInProgressMarkets == 1u);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);

	// Should not request another because in-progress limit reached
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "in_progress_limit_reached");

	printf("PASS: TestInProgressConcurrencyLimit\n");
}

// Test: High cash allows higher concurrency
static void TestHighCashAllowsHigherConcurrency()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 12000u; // High cash
	input.smoothedNetCashPerMinute = 1200;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 8;
	input.inProgressBlackMarkets = 0;
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should allow multiple in-progress markets when cash is floating
	assert(policy.maxInProgressMarkets >= 2u);

	printf("PASS: TestHighCashAllowsHigherConcurrency\n");
}

// Test: Hard cap blocks further scaling
static void TestHardCapBlocksScaling()
{
	AIControlAdapterEconomyManager manager;

	EconomyManagerInput input;
	input.currentMoney = 10000u;
	input.smoothedNetCashPerMinute = 1500;
	input.reserveCash = 3000u;
	input.completedBlackMarkets = 30;
	input.inProgressBlackMarkets = 2;
	input.hasPalace = true;
	input.profile = "sprawl_balanced";

	EconomyPolicy policy = manager.AssessEconomyPolicy(input);

	// Should be saturated when at hard cap
	assert(policy.scalingMode == EconomyScalingMode::Saturated);

	EconomyRecoveryRequest request = manager.ChooseRecoveryAction(input, policy);
	assert(request.shouldBuildIncome == false);
	assert(request.reason == "black_market_cap_reached");

	printf("PASS: TestHardCapBlocksScaling\n");
}

static void TestGlobalWorkerLiquidityPassBlocksReserveAndCap()
{
	AIControlAdapterEconomyManager manager;

	AIControlAdapterLocalWorkerLiquidityResult reserveBlocked =
		manager.ChooseGlobalWorkerLiquidityPass(20, 80, 2000u);
	assert(reserveBlocked.shouldQueue == false);
	assert(std::string(reserveBlocked.reason) == "cash_reserved");

	AIControlAdapterLocalWorkerLiquidityResult capBlocked =
		manager.ChooseGlobalWorkerLiquidityPass(80, 80, 5000u);
	assert(capBlocked.shouldQueue == false);
	assert(std::string(capBlocked.reason) == "worker_cap_reached");

	AIControlAdapterLocalWorkerLiquidityResult allowed =
		manager.ChooseGlobalWorkerLiquidityPass(20, 80, 5000u);
	assert(allowed.shouldQueue == true);
	assert(std::string(allowed.reason) == "queued_local_worker");

	printf("PASS: TestGlobalWorkerLiquidityPassBlocksReserveAndCap\n");
}

static void TestLocalWorkerLiquidityDesiredCounts()
{
	AIControlAdapterEconomyManager manager;

	LocalWorkerZoneInput input;
	input.globalWorkers = 20;
	input.workerCap = 80;
	input.cashFloat = 5000u;
	input.localIdleWorkers = 0;
	input.hasLocalProducer = true;

	LocalWorkerZoneDecision undeveloped = manager.ChooseLocalWorkerLiquidityForZone(input);
	assert(undeveloped.desiredLocalWorkers == 0);
	assert(undeveloped.policy.shouldQueue == false);
	assert(std::string(undeveloped.policy.reason) == "target_met");

	input.developed = true;
	LocalWorkerZoneDecision developed = manager.ChooseLocalWorkerLiquidityForZone(input);
	assert(developed.desiredLocalWorkers == 1);
	assert(developed.policy.shouldQueue == true);

	input.active = true;
	LocalWorkerZoneDecision active = manager.ChooseLocalWorkerLiquidityForZone(input);
	assert(active.desiredLocalWorkers == 2);
	assert(active.policy.shouldQueue == true);

	input.hasLocalStrategicTask = true;
	LocalWorkerZoneDecision strategic = manager.ChooseLocalWorkerLiquidityForZone(input);
	assert(strategic.desiredLocalWorkers == 3);
	assert(strategic.policy.shouldQueue == true);

	input.localIdleWorkers = 3;
	LocalWorkerZoneDecision targetMet = manager.ChooseLocalWorkerLiquidityForZone(input);
	assert(targetMet.desiredLocalWorkers == 3);
	assert(targetMet.policy.shouldQueue == false);
	assert(std::string(targetMet.policy.reason) == "target_met");

	input.localIdleWorkers = 0;
	input.hasLocalProducer = false;
	LocalWorkerZoneDecision producerMissing = manager.ChooseLocalWorkerLiquidityForZone(input);
	assert(producerMissing.desiredLocalWorkers == 3);
	assert(producerMissing.policy.shouldQueue == false);
	assert(std::string(producerMissing.policy.reason) == "producer_missing");

	printf("PASS: TestLocalWorkerLiquidityDesiredCounts\n");
}

static void TestWorkerProductionDecision()
{
	AIControlAdapterEconomyManager manager;

	WorkerProductionDecision targetMet = manager.ChooseWorkerProduction(3, 2, 1);
	assert(targetMet.shouldQueue == false);
	assert(targetMet.queueCount == 0);
	assert(std::string(targetMet.reason) == "target_met");

	WorkerProductionDecision disabled = manager.ChooseWorkerProduction(0, 0, 1);
	assert(disabled.shouldQueue == false);
	assert(disabled.queueCount == 0);
	assert(std::string(disabled.reason) == "target_met");

	WorkerProductionDecision deficit = manager.ChooseWorkerProduction(0, 2, 3);
	assert(deficit.shouldQueue == true);
	assert(deficit.queueCount == 3);
	assert(std::string(deficit.reason) == "idle_worker_deficit");

	WorkerProductionDecision sanitized = manager.ChooseWorkerProduction(0, 2, 0);
	assert(sanitized.shouldQueue == true);
	assert(sanitized.queueCount == 1);
	assert(std::string(sanitized.reason) == "idle_worker_deficit");

	printf("PASS: TestWorkerProductionDecision\n");
}

static void TestStashWorkerProductionDecision()
{
	AIControlAdapterEconomyManager manager;

	StashWorkerProductionDecision noStashes =
		manager.ChooseStashWorkerProduction({}, {}, 2);
	assert(noStashes.shouldQueue == false);
	assert(noStashes.targetStashId == 0);
	assert(noStashes.queueCount == 0);
	assert(std::string(noStashes.reason) == "no_stashes");

	StashWorkerProductionDecision firstUnserviced =
		manager.ChooseStashWorkerProduction({101, 202, 303}, {101}, 2);
	assert(firstUnserviced.shouldQueue == true);
	assert(firstUnserviced.targetStashId == 202);
	assert(firstUnserviced.queueCount == 2);
	assert(std::string(firstUnserviced.reason) == "unserviced_stash");

	StashWorkerProductionDecision sanitized =
		manager.ChooseStashWorkerProduction({101}, {}, 0);
	assert(sanitized.shouldQueue == true);
	assert(sanitized.targetStashId == 101);
	assert(sanitized.queueCount == 1);
	assert(std::string(sanitized.reason) == "unserviced_stash");

	StashWorkerProductionDecision targetMet =
		manager.ChooseStashWorkerProduction({101, 202}, {101, 202}, 2);
	assert(targetMet.shouldQueue == false);
	assert(targetMet.targetStashId == 0);
	assert(targetMet.queueCount == 0);
	assert(std::string(targetMet.reason) == "target_met");

	printf("PASS: TestStashWorkerProductionDecision\n");
}

int main()
{
	TestHealthyEconomy();
	TestWeakIncomeProtectedReserve();
	TestCriticalIncomePressuredReserve();
	TestCriticalIncomeDepletedReserve();
	TestMissingPalace();
	TestBlackMarketCapReached();
	TestInProgressBlackMarkets();
	TestNoMoney();
	TestSufficientIncomeStopsRecovery();
	TestWeakIncomeGradualExpansion();
	TestTechProfileLowerCaps();
	TestEmergencyBuildAllowedWhenReservePressured();
	TestBurstIncomeDepletedReserve();
	TestRecoveringHeldWhileReserveDepleted();
	TestPossibleBurstPressuredReserve();

	// Phase 5.6: Continuous Economy Scaling tests
	TestSprawlHighHardCap();
	TestGrowthContinuesAfterReserveProtected();
	TestDepletedReserveTriggersHighPriority();
	TestShortTermIncomeDoesNotStopScaling();
	TestInProgressConcurrencyLimit();
	TestHighCashAllowsHigherConcurrency();
	TestHardCapBlocksScaling();
	TestGlobalWorkerLiquidityPassBlocksReserveAndCap();
	TestLocalWorkerLiquidityDesiredCounts();
	TestWorkerProductionDecision();
	TestStashWorkerProductionDecision();

	printf("All EconomyManager tests passed.\n");
	return 0;
}
