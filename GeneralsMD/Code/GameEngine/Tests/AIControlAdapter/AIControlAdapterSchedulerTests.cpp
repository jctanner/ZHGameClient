/**
 * AIControlAdapterSchedulerTests.cpp
 *
 * Unit tests for AIControlAdapterScheduler intent arbitration and budget enforcement.
 *
 * Test coverage:
 * - Priority ordering (CRITICAL > HIGH > NORMAL > LOW)
 * - Category budget enforcement
 * - Failed intent doesn't block unrelated intents
 * - Tech + production arbitration
 * - Budget reset between ticks
 */

#include "GameClient/AIControlAdapter/AIControlAdapterScheduler.h"

#include <cassert>
#include <cstdio>
#include <vector>
#include <string>

// Test helper: create intent with execute function
Intent MakeIntent(
	IntentCategory category,
	IntentPriority priority,
	const char* commandName,
	const char* targetName,
	bool shouldSucceed)
{
	Intent intent;
	intent.category = category;
	intent.priority = priority;
	intent.commandName = commandName;
	intent.targetName = targetName;
	intent.reason = "test_intent";

	intent.executeFunc = [shouldSucceed](std::string& resultReason) -> bool {
		if (shouldSucceed)
		{
			resultReason = "success";
			return true;
		}
		else
		{
			resultReason = "failed";
			return false;
		}
	};

	return intent;
}

// Test: Intents are processed in priority order
void TestPriorityOrdering()
{
	printf("Running TestPriorityOrdering...\n");

	AIControlAdapterScheduler scheduler;

	// Submit intents in reverse priority order
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::LOW, "cmd1", "target1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "cmd2", "target2", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::HIGH, "cmd3", "target3", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::CRITICAL, "cmd4", "target4", true));

	std::vector<IntentResult> results = scheduler.ProcessIntents(1000);

	// Verify results are in priority order (CRITICAL -> HIGH -> NORMAL -> LOW)
	assert(results.size() == 4);
	assert(results[0].commandName == "cmd4"); // CRITICAL
	assert(results[0].priority == IntentPriority::CRITICAL);
	assert(results[1].commandName == "cmd3"); // HIGH
	assert(results[1].priority == IntentPriority::HIGH);
	assert(results[2].commandName == "cmd2"); // NORMAL
	assert(results[2].priority == IntentPriority::NORMAL);

	// LOW intent should be skipped due to production budget (max 3)
	assert(results[3].commandName == "cmd1");
	assert(results[3].state == IntentState::SKIPPED);
	assert(results[3].resultReason == "category_budget_exhausted");

	printf("  PASSED: Intents processed in priority order\n");
}

// Test: Category budget limits are enforced
void TestCategoryBudgetLimits()
{
	printf("Running TestCategoryBudgetLimits...\n");

	SchedulerConfig config;
	config.categoryBudgets.clear();
	config.categoryBudgets.push_back(CategoryBudget(IntentCategory::TECH_SCIENCE, 1));
	config.categoryBudgets.push_back(CategoryBudget(IntentCategory::PRODUCTION, 2));

	AIControlAdapterScheduler scheduler(config);

	// Submit 3 science intents (budget allows 1)
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci1", "science1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci2", "science2", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci3", "science3", true));

	// Submit 3 production intents (budget allows 2)
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod1", "unit1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod2", "unit2", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod3", "unit3", true));

	std::vector<IntentResult> results = scheduler.ProcessIntents(1000);

	// Count attempted vs skipped by category
	int scienceAttempted = 0;
	int scienceSkipped = 0;
	int productionAttempted = 0;
	int productionSkipped = 0;

	for (const IntentResult& result : results)
	{
		if (result.category == IntentCategory::TECH_SCIENCE)
		{
			if (result.state == IntentState::ATTEMPTED)
				scienceAttempted++;
			else if (result.state == IntentState::SKIPPED)
				scienceSkipped++;
		}
		else if (result.category == IntentCategory::PRODUCTION)
		{
			if (result.state == IntentState::ATTEMPTED)
				productionAttempted++;
			else if (result.state == IntentState::SKIPPED)
				productionSkipped++;
		}
	}

	assert(scienceAttempted == 1);
	assert(scienceSkipped == 2);
	assert(productionAttempted == 2);
	assert(productionSkipped == 1);

	printf("  PASSED: Category budgets enforced correctly\n");
}

// Test: Failed intent doesn't block unrelated intents when budget allows
void TestFailedIntentContinuation()
{
	printf("Running TestFailedIntentContinuation...\n");

	SchedulerConfig config;
	config.categoryBudgets.clear();
	config.categoryBudgets.push_back(CategoryBudget(IntentCategory::TECH_SCIENCE, 3)); // Allow 3 attempts

	AIControlAdapterScheduler scheduler(config);

	// Submit 3 science intents: first fails, others succeed
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci1", "science1", false)); // FAIL
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci2", "science2", true));  // SUCCESS
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci3", "science3", true));  // SUCCESS

	std::vector<IntentResult> results = scheduler.ProcessIntents(1000);

	assert(results.size() == 3);

	// First intent should be attempted and failed
	assert(results[0].attempted == true);
	assert(results[0].issued == false);
	assert(results[0].resultReason == "failed");

	// Second intent should be attempted and succeed (not blocked by first failure)
	assert(results[1].attempted == true);
	assert(results[1].issued == true);
	assert(results[1].resultReason == "success");

	// Third intent should be attempted and succeed
	assert(results[2].attempted == true);
	assert(results[2].issued == true);
	assert(results[2].resultReason == "success");

	printf("  PASSED: Failed intent doesn't block continuation within budget\n");
}

// Test: Tech + production arbitration with mixed priorities
void TestTechProductionArbitration()
{
	printf("Running TestTechProductionArbitration...\n");

	AIControlAdapterScheduler scheduler;

	// Submit intents with mixed categories and priorities
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod1", "unit1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_SCIENCE, IntentPriority::HIGH, "sci1", "science1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::TECH_UPGRADE, IntentPriority::HIGH, "upg1", "upgrade1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::LOW, "prod2", "unit2", true));

	std::vector<IntentResult> results = scheduler.ProcessIntents(1000);

	// Verify processing order respects priority across categories:
	// 1. TECH_SCIENCE (HIGH)
	// 2. TECH_UPGRADE (HIGH)
	// 3. PRODUCTION (NORMAL)
	// 4. PRODUCTION (LOW) - should be skipped due to production budget (max 3, but only 1 production attempted)

	assert(results.size() == 4);
	assert(results[0].category == IntentCategory::TECH_SCIENCE);
	assert(results[0].commandName == "sci1");
	assert(results[0].attempted == true);

	assert(results[1].category == IntentCategory::TECH_UPGRADE);
	assert(results[1].commandName == "upg1");
	assert(results[1].attempted == true);

	assert(results[2].category == IntentCategory::PRODUCTION);
	assert(results[2].commandName == "prod1");
	assert(results[2].attempted == true);

	assert(results[3].category == IntentCategory::PRODUCTION);
	assert(results[3].commandName == "prod2");
	// prod2 should still be attempted since production budget is 3
	assert(results[3].attempted == true);

	printf("  PASSED: Tech and production arbitrate correctly by priority\n");
}

// Test: Budget resets between ticks
void TestBudgetReset()
{
	printf("Running TestBudgetReset...\n");

	SchedulerConfig config;
	config.categoryBudgets.clear();
	config.categoryBudgets.push_back(CategoryBudget(IntentCategory::PRODUCTION, 1));

	AIControlAdapterScheduler scheduler(config);

	// Tick 1: Submit 2 intents (budget allows 1)
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::HIGH, "prod1", "unit1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod2", "unit2", true));

	std::vector<IntentResult> results1 = scheduler.ProcessIntents(1000);
	assert(results1.size() == 2);
	assert(results1[0].attempted == true);   // First attempted
	assert(results1[1].state == IntentState::SKIPPED); // Second skipped

	// Tick 2: Submit 2 more intents (budget should be reset)
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::HIGH, "prod3", "unit3", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod4", "unit4", true));

	std::vector<IntentResult> results2 = scheduler.ProcessIntents(2000);
	assert(results2.size() == 2);
	assert(results2[0].attempted == true);   // First attempted (budget reset)
	assert(results2[1].state == IntentState::SKIPPED); // Second skipped

	printf("  PASSED: Budget resets correctly between ticks\n");
}

// Test: Defense category has separate budget from production
void TestDefenseSeparateBudget()
{
	printf("Running TestDefenseSeparateBudget...\n");

	AIControlAdapterScheduler scheduler;

	// Submit defense intent (CRITICAL priority)
	scheduler.SubmitIntent(MakeIntent(IntentCategory::DEFENSE, IntentPriority::CRITICAL, "def1", "defender1", true));

	// Submit production intents (NORMAL priority)
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod1", "unit1", true));
	scheduler.SubmitIntent(MakeIntent(IntentCategory::PRODUCTION, IntentPriority::NORMAL, "prod2", "unit2", true));

	std::vector<IntentResult> results = scheduler.ProcessIntents(1000);

	// Defense should be attempted (CRITICAL priority, separate budget)
	// Production should be attempted (NORMAL priority, separate budget)
	assert(results.size() == 3);
	assert(results[0].category == IntentCategory::DEFENSE);
	assert(results[0].attempted == true);
	assert(results[1].category == IntentCategory::PRODUCTION);
	assert(results[1].attempted == true);
	assert(results[2].category == IntentCategory::PRODUCTION);
	assert(results[2].attempted == true);

	printf("  PASSED: Defense has separate budget from production\n");
}

// Test: Intent result includes all telemetry fields
void TestIntentResultTelemetry()
{
	printf("Running TestIntentResultTelemetry...\n");

	AIControlAdapterScheduler scheduler;

	Intent intent = MakeIntent(IntentCategory::PRODUCTION, IntentPriority::HIGH, "Game.QueueQuad", "GLAVehicleQuad", true);
	intent.producerObjectId = 12345;
	intent.producerKind = "arms_dealer";

	scheduler.SubmitIntent(intent);

	std::vector<IntentResult> results = scheduler.ProcessIntents(5000);

	assert(results.size() == 1);
	const IntentResult& result = results[0];

	// Verify all telemetry fields are populated
	assert(result.category == IntentCategory::PRODUCTION);
	assert(result.priority == IntentPriority::HIGH);
	assert(result.state == IntentState::ATTEMPTED);
	assert(result.commandName == "Game.QueueQuad");
	assert(result.targetName == "GLAVehicleQuad");
	assert(result.attempted == true);
	assert(result.issued == true);
	assert(result.resultReason == "success");
	assert(result.tick == 5000);
	assert(result.producerObjectId == 12345);
	assert(result.producerKind == "arms_dealer");

	printf("  PASSED: Intent result includes all telemetry fields\n");
}

void TestAutonomyTickScheduleAllZeroDue()
{
	printf("Running TestAutonomyTickScheduleAllZeroDue...\n");

	AIControlAdapterAutonomyTickSchedule schedule =
		AIControlAdapterScheduler::EvaluateAutonomyTickSchedule(1000u, 0u, 0u, 0u, 0u);

	assert(schedule.macroDue == true);
	assert(schedule.productionDue == true);
	assert(schedule.techDue == true);
	assert(schedule.guardDue == true);
	assert(schedule.anyDue == true);
	assert(schedule.guardOnly == false);

	printf("  PASSED: Zero tick deadlines are due\n");
}

void TestAutonomyTickScheduleFutureTimersSkip()
{
	printf("Running TestAutonomyTickScheduleFutureTimersSkip...\n");

	AIControlAdapterAutonomyTickSchedule schedule =
		AIControlAdapterScheduler::EvaluateAutonomyTickSchedule(1000u, 2000u, 3000u, 4000u, 5000u);

	assert(schedule.macroDue == false);
	assert(schedule.productionDue == false);
	assert(schedule.techDue == false);
	assert(schedule.guardDue == false);
	assert(schedule.anyDue == false);
	assert(schedule.guardOnly == false);

	printf("  PASSED: Future tick deadlines are not due\n");
}

void TestAutonomyTickScheduleGuardOnly()
{
	printf("Running TestAutonomyTickScheduleGuardOnly...\n");

	AIControlAdapterAutonomyTickSchedule schedule =
		AIControlAdapterScheduler::EvaluateAutonomyTickSchedule(5000u, 6000u, 7000u, 8000u, 4000u);

	assert(schedule.macroDue == false);
	assert(schedule.productionDue == false);
	assert(schedule.techDue == false);
	assert(schedule.guardDue == true);
	assert(schedule.anyDue == true);
	assert(schedule.guardOnly == true);

	printf("  PASSED: Guard-only schedule is identified\n");
}

void TestAutonomyTickScheduleProductionBreaksGuardOnly()
{
	printf("Running TestAutonomyTickScheduleProductionBreaksGuardOnly...\n");

	AIControlAdapterAutonomyTickSchedule schedule =
		AIControlAdapterScheduler::EvaluateAutonomyTickSchedule(5000u, 6000u, 5000u, 8000u, 4000u);

	assert(schedule.macroDue == false);
	assert(schedule.productionDue == true);
	assert(schedule.techDue == false);
	assert(schedule.guardDue == true);
	assert(schedule.anyDue == true);
	assert(schedule.guardOnly == false);

	printf("  PASSED: Production-due schedule is not guard-only\n");
}

int main()
{
	printf("AIControlAdapterScheduler Tests\n");
	printf("================================\n\n");

	TestPriorityOrdering();
	TestCategoryBudgetLimits();
	TestFailedIntentContinuation();
	TestTechProductionArbitration();
	TestBudgetReset();
	TestDefenseSeparateBudget();
	TestIntentResultTelemetry();
	TestAutonomyTickScheduleAllZeroDue();
	TestAutonomyTickScheduleFutureTimersSkip();
	TestAutonomyTickScheduleGuardOnly();
	TestAutonomyTickScheduleProductionBreaksGuardOnly();

	printf("\n================================\n");
	printf("All tests passed!\n");

	return 0;
}
