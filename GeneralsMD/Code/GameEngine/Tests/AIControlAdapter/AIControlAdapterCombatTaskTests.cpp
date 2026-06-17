#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

//==============================================================================
// Phase 9: Combat Task Ownership Tests
//==============================================================================

namespace
{
	AIControlAdapterCombatTaskManager manager;

	struct TestCase
	{
		const char* name;
		void (*fn)();
	};

	std::vector<TestCase>& testCases()
	{
		static std::vector<TestCase> cases;
		return cases;
	}

	struct TestRegistration
	{
		TestRegistration(const char* name, void (*fn)())
		{
			testCases().push_back({name, fn});
		}
	};

	template <typename Actual, typename Expected>
	void expectEqual(const Actual& actual, const Expected& expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (!(actual == expected))
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " == " << expectedExpr << "\n";
			std::exit(1);
		}
	}

	template <typename Actual, typename Expected>
	void expectNotEqual(const Actual& actual, const Expected& expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (!(actual != expected))
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " != " << expectedExpr << "\n";
			std::exit(1);
		}
	}

	template <typename Actual, typename Expected>
	void expectGreater(const Actual& actual, const Expected& expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (!(actual > expected))
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " > " << expectedExpr << "\n";
			std::exit(1);
		}
	}

	template <typename Actual, typename Expected>
	void expectGreaterEqual(const Actual& actual, const Expected& expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (!(actual >= expected))
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " >= " << expectedExpr << "\n";
			std::exit(1);
		}
	}

	template <typename Actual, typename Expected>
	void expectLessEqual(const Actual& actual, const Expected& expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (!(actual <= expected))
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " <= " << expectedExpr << "\n";
			std::exit(1);
		}
	}

	void expectTrue(bool condition, const char* expr, const char* file, int line)
	{
		if (!condition)
		{
			std::cerr << file << ":" << line << ": expected true: " << expr << "\n";
			std::exit(1);
		}
	}

	void expectFalse(bool condition, const char* expr, const char* file, int line)
	{
		if (condition)
		{
			std::cerr << file << ":" << line << ": expected false: " << expr << "\n";
			std::exit(1);
		}
	}

	void expectFloatEqual(float actual, float expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (std::fabs(actual - expected) > 0.0001f)
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " ~= " << expectedExpr
				<< " actual=" << actual << " expected=" << expected << "\n";
			std::exit(1);
		}
	}

	void expectStringEqual(const char* actual, const char* expected, const char* actualExpr, const char* expectedExpr, const char* file, int line)
	{
		if (std::strcmp(actual, expected) != 0)
		{
			std::cerr << file << ":" << line << ": expected " << actualExpr << " == " << expectedExpr
				<< " actual=" << actual << " expected=" << expected << "\n";
			std::exit(1);
		}
	}
}

#define TEST_F(Fixture, Name) \
	static void Fixture##_##Name##_impl(); \
	namespace { TestRegistration Fixture##_##Name##_registration(#Fixture "." #Name, Fixture##_##Name##_impl); } \
	static void Fixture##_##Name##_impl()

#define EXPECT_EQ(actual, expected) expectEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define ASSERT_EQ(actual, expected) expectEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_NE(actual, expected) expectNotEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define ASSERT_NE(actual, expected) expectNotEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_GT(actual, expected) expectGreater((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_GE(actual, expected) expectGreaterEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_LE(actual, expected) expectLessEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_TRUE(condition) expectTrue((condition), #condition, __FILE__, __LINE__)
#define EXPECT_FALSE(condition) expectFalse((condition), #condition, __FILE__, __LINE__)
#define EXPECT_FLOAT_EQ(actual, expected) expectFloatEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_STREQ(actual, expected) expectStringEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

TEST_F(AIControlAdapterCombatTaskTests, CreateTask_CreatesTaskWithUniqueId)
{
	std::vector<unsigned int> units1 = {100, 101, 102};
	std::vector<unsigned int> units2 = {200, 201, 202};

	Coord3D target1{1000.0f, 2000.0f, 0.0f};
	Coord3D target2{1500.0f, 2500.0f, 0.0f};

	const unsigned int taskId1 = manager.createTask(
		CombatTaskType::Attack,
		units1,
		target1,
		"autonomous_attack",
		"test_attack_1",
		120000);

	const unsigned int taskId2 = manager.createTask(
		CombatTaskType::Defense,
		units2,
		target2,
		"zone_defense",
		"test_defense_1",
		90000);

	EXPECT_GT(taskId1, 0u);
	EXPECT_GT(taskId2, 0u);
	EXPECT_NE(taskId1, taskId2);
}

TEST_F(AIControlAdapterCombatTaskTests, CreateTask_InitializesStateToAssembling)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	const CombatTask* task = manager.findTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, CombatTaskState::Assembling);
	EXPECT_EQ(task->type, CombatTaskType::Attack);
	EXPECT_EQ(task->owner, "autonomous_attack");
	EXPECT_EQ(task->reason, "test_reason");
	EXPECT_EQ(task->assignedUnitIds.size(), 3u);
	EXPECT_EQ(task->initialUnitCount, 3);
	EXPECT_GE(task->minimumViableCount, 1);
}

TEST_F(AIControlAdapterCombatTaskTests, CreateTask_SetsMinimumViableCountTo30Percent)
{
	std::vector<unsigned int> units = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	const CombatTask* task = manager.findTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->initialUnitCount, 10);
	EXPECT_EQ(task->minimumViableCount, 3); // 30% of 10 = 3
}

TEST_F(AIControlAdapterCombatTaskTests, IsUnitReserved_ReturnsTrueForReservedUnit)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	EXPECT_TRUE(manager.isUnitReserved(100));
	EXPECT_TRUE(manager.isUnitReserved(101));
	EXPECT_TRUE(manager.isUnitReserved(102));
	EXPECT_FALSE(manager.isUnitReserved(103));
}

TEST_F(AIControlAdapterCombatTaskTests, IsUnitReserved_ReturnsFalseForCompletedTask)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	EXPECT_TRUE(manager.isUnitReserved(100));

	manager.completeTask(taskId, "objective_reached");

	EXPECT_FALSE(manager.isUnitReserved(100));
}

TEST_F(AIControlAdapterCombatTaskTests, IsUnitReserved_ReturnsFalseForFailedTask)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	EXPECT_TRUE(manager.isUnitReserved(100));

	manager.failTask(taskId, "too_many_casualties");

	EXPECT_FALSE(manager.isUnitReserved(100));
}

TEST_F(AIControlAdapterCombatTaskTests, CanUseUnitForTask_AllowsSameOwner)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	EXPECT_TRUE(manager.canUseUnitForTask(100, "autonomous_attack"));
	EXPECT_FALSE(manager.canUseUnitForTask(100, "different_owner"));
	EXPECT_TRUE(manager.canUseUnitForTask(103, "any_owner"));
}

TEST_F(AIControlAdapterCombatTaskTests, RemoveDeadUnits_RemovesUnitsFromTask)
{
	std::vector<unsigned int> units = {100, 101, 102, 103, 104};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	std::vector<unsigned int> deadUnits = {101, 103};
	const bool result = manager.removeDeadUnits(taskId, deadUnits);

	EXPECT_TRUE(result);

	const CombatTask* task = manager.findTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->assignedUnitIds.size(), 3u); // Started with 5, removed 2
	EXPECT_FALSE(manager.isUnitReserved(101));
	EXPECT_FALSE(manager.isUnitReserved(103));
	EXPECT_TRUE(manager.isUnitReserved(100));
	EXPECT_TRUE(manager.isUnitReserved(102));
	EXPECT_TRUE(manager.isUnitReserved(104));
}

TEST_F(AIControlAdapterCombatTaskTests, UpdateTaskState_ChangesState)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	const bool result = manager.updateTaskState(taskId, CombatTaskState::Moving, "moving_to_target");

	EXPECT_TRUE(result);

	const CombatTask* task = manager.findTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, CombatTaskState::Moving);
	EXPECT_EQ(task->reason, "moving_to_target");
}

TEST_F(AIControlAdapterCombatTaskTests, HasEquivalentActiveTask_DetectsDuplicates)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	Coord3D nearbyTarget{1100.0f, 2100.0f, 0.0f};
	Coord3D farTarget{5000.0f, 5000.0f, 0.0f};

	EXPECT_TRUE(manager.hasEquivalentActiveTask(CombatTaskType::Attack, nearbyTarget, 500.0f));
	EXPECT_FALSE(manager.hasEquivalentActiveTask(CombatTaskType::Attack, farTarget, 500.0f));
	EXPECT_FALSE(manager.hasEquivalentActiveTask(CombatTaskType::Defense, nearbyTarget, 500.0f));
}

TEST_F(AIControlAdapterCombatTaskTests, HasEquivalentActiveTask_IgnoresCompletedTasks)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	EXPECT_TRUE(manager.hasEquivalentActiveTask(CombatTaskType::Attack, target, 500.0f));

	manager.completeTask(taskId, "objective_reached");

	EXPECT_FALSE(manager.hasEquivalentActiveTask(CombatTaskType::Attack, target, 500.0f));
}

TEST_F(AIControlAdapterCombatTaskTests, GetActiveTaskCount_CountsOnlyActiveTasks)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	EXPECT_EQ(manager.getActiveTaskCount(), 0);

	const unsigned int taskId1 = manager.createTask(
		CombatTaskType::Attack, units, target, "autonomous_attack", "test_1", 120000);

	EXPECT_EQ(manager.getActiveTaskCount(), 1);

	const unsigned int taskId2 = manager.createTask(
		CombatTaskType::Defense, units, target, "zone_defense", "test_2", 90000);

	EXPECT_EQ(manager.getActiveTaskCount(), 2);

	manager.completeTask(taskId1, "done");

	EXPECT_EQ(manager.getActiveTaskCount(), 1);

	manager.failTask(taskId2, "failed");

	EXPECT_EQ(manager.getActiveTaskCount(), 0);
}

TEST_F(AIControlAdapterCombatTaskTests, GetAttackTaskCount_CountsOnlyAttackTasks)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	manager.createTask(CombatTaskType::Attack, units, target, "autonomous_attack", "test_1", 120000);
	manager.createTask(CombatTaskType::Attack, units, target, "autonomous_attack", "test_2", 120000);
	manager.createTask(CombatTaskType::Defense, units, target, "zone_defense", "test_3", 90000);

	EXPECT_EQ(manager.getAttackTaskCount(), 2);
	EXPECT_EQ(manager.getDefenseTaskCount(), 1);
}

TEST_F(AIControlAdapterCombatTaskTests, FindTaskByUnit_FindsCorrectTask)
{
	std::vector<unsigned int> units1 = {100, 101, 102};
	std::vector<unsigned int> units2 = {200, 201, 202};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId1 = manager.createTask(
		CombatTaskType::Attack, units1, target, "autonomous_attack", "test_1", 120000);
	const unsigned int taskId2 = manager.createTask(
		CombatTaskType::Defense, units2, target, "zone_defense", "test_2", 90000);

	const CombatTask* task1 = manager.findTaskByUnit(101);
	ASSERT_NE(task1, nullptr);
	EXPECT_EQ(task1->taskId, taskId1);
	EXPECT_EQ(task1->type, CombatTaskType::Attack);

	const CombatTask* task2 = manager.findTaskByUnit(201);
	ASSERT_NE(task2, nullptr);
	EXPECT_EQ(task2->taskId, taskId2);
	EXPECT_EQ(task2->type, CombatTaskType::Defense);

	const CombatTask* task3 = manager.findTaskByUnit(999);
	EXPECT_EQ(task3, nullptr);
}

TEST_F(AIControlAdapterCombatTaskTests, UpdateTasks_MarksExpiredTasks)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		1000); // 1 second timeout

	const CombatTask* task = manager.findTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, CombatTaskState::Assembling);

	// Simulate time passing beyond timeout
	const DWORD currentTick = task->createdTick + 2000; // 2 seconds later

	manager.updateTasks(currentTick);

	const CombatTask* expiredTask = manager.findTask(taskId);
	ASSERT_NE(expiredTask, nullptr);
	EXPECT_EQ(expiredTask->state, CombatTaskState::Expired);
	EXPECT_EQ(expiredTask->reason, "timeout");
}

TEST_F(AIControlAdapterCombatTaskTests, PruneExpiredTasks_RemovesTerminalTasksAfterGracePeriod)
{
	std::vector<unsigned int> units = {100, 101, 102};
	Coord3D target{1000.0f, 2000.0f, 0.0f};

	const unsigned int taskId = manager.createTask(
		CombatTaskType::Attack,
		units,
		target,
		"autonomous_attack",
		"test_reason",
		120000);

	manager.completeTask(taskId, "done");

	const CombatTask* completedTask = manager.findTask(taskId);
	ASSERT_NE(completedTask, nullptr);
	EXPECT_EQ(completedTask->state, CombatTaskState::Complete);

	// Immediately after completion, task should still exist
	manager.pruneExpiredTasks(completedTask->lastCommandTick);
	EXPECT_NE(manager.findTask(taskId), nullptr);

	// After 5 second grace period, task should be pruned
	const DWORD laterTick = completedTask->lastCommandTick + 6000;
	manager.pruneExpiredTasks(laterTick);
	EXPECT_EQ(manager.findTask(taskId), nullptr);
}

TEST_F(AIControlAdapterCombatTaskTests, BuildDirectRaidWaypoints_CreatesDeterministicStages)
{
	Coord3D origin{0.0f, 0.0f, 0.0f};
	Coord3D target{1000.0f, 0.0f, 0.0f};

	std::vector<CombatTaskWaypoint> waypoints = buildDirectRaidWaypoints(origin, target, 300.0f);

	ASSERT_EQ(waypoints.size(), 3u);
	EXPECT_FLOAT_EQ(waypoints[0].position.x, 350.0f);
	EXPECT_FLOAT_EQ(waypoints[1].position.x, 760.0f);
	EXPECT_FLOAT_EQ(waypoints[2].position.x, 1000.0f);
	EXPECT_GE(waypoints[0].radius, 250.0f);
	EXPECT_LE(waypoints[0].radius, 350.0f);
}

TEST_F(AIControlAdapterCombatTaskTests, Cohesion_MixedGroupWaitsForInfantryQuorum)
{
	CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
		10, // live assigned
		8,  // arrived
		4,  // infantry live
		1,  // infantry arrived
		3,  // minimum viable
		false
	});

	EXPECT_FALSE(decision.shouldAdvance);
	EXPECT_FALSE(decision.shouldFail);
	EXPECT_STREQ(decision.reason, "waiting_for_infantry_quorum");
	EXPECT_EQ(decision.requiredQuorum, 7);
	EXPECT_EQ(decision.infantryRequiredQuorum, 2);
}

TEST_F(AIControlAdapterCombatTaskTests, Cohesion_VehicleOnlyAdvancesOnGroupQuorum)
{
	CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
		10,
		7,
		0,
		0,
		3,
		false
	});

	EXPECT_TRUE(decision.shouldAdvance);
	EXPECT_FALSE(decision.shouldFail);
	EXPECT_STREQ(decision.reason, "quorum_reached");
}

TEST_F(AIControlAdapterCombatTaskTests, Cohesion_VehicleRaidDoesNotWaitForInfantryQuorum)
{
	CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
		10,
		7,
		4,
		0,
		3,
		false,
		false
	});

	EXPECT_TRUE(decision.shouldAdvance);
	EXPECT_FALSE(decision.shouldFail);
	EXPECT_STREQ(decision.reason, "quorum_reached");
	EXPECT_EQ(decision.requiredQuorum, 7);
}

TEST_F(AIControlAdapterCombatTaskTests, RaidMix_LongDistancePrefersVehiclesAndExcludesInfantry)
{
	CombatTaskRaidMixDecision decision = evaluateCombatTaskRaidMixPolicy(8, 12, 3200.0f, 1400.0f);

	EXPECT_TRUE(decision.shouldLaunch);
	EXPECT_TRUE(decision.allowVehicles);
	EXPECT_FALSE(decision.allowInfantry);
	EXPECT_STREQ(decision.mode, "vehicle");
	EXPECT_STREQ(decision.reason, "long_distance_vehicle_only");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidMix_LocalInfantryOnlyCanLaunch)
{
	CombatTaskRaidMixDecision decision = evaluateCombatTaskRaidMixPolicy(0, 8, 800.0f, 1400.0f);

	EXPECT_TRUE(decision.shouldLaunch);
	EXPECT_FALSE(decision.allowVehicles);
	EXPECT_TRUE(decision.allowInfantry);
	EXPECT_STREQ(decision.mode, "infantry");
	EXPECT_STREQ(decision.reason, "local_infantry_push");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidMix_LongDistanceInfantryOnlyHolds)
{
	CombatTaskRaidMixDecision decision = evaluateCombatTaskRaidMixPolicy(0, 8, 3200.0f, 1400.0f);

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_FALSE(decision.allowVehicles);
	EXPECT_FALSE(decision.allowInfantry);
	EXPECT_STREQ(decision.mode, "hold");
	EXPECT_STREQ(decision.reason, "long_distance_infantry_blocked");
}

TEST_F(AIControlAdapterCombatTaskTests, Cohesion_ConfirmedDeadUnitsReduceDenominator)
{
	CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
		7,
		5,
		0,
		0,
		3,
		false
	});

	EXPECT_TRUE(decision.shouldAdvance);
	EXPECT_EQ(decision.requiredQuorum, 5);
}

TEST_F(AIControlAdapterCombatTaskTests, Cohesion_TimeoutAdvancesWhenViable)
{
	CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
		8,
		3,
		2,
		0,
		3,
		true
	});

	EXPECT_TRUE(decision.shouldAdvance);
	EXPECT_FALSE(decision.shouldFail);
	EXPECT_STREQ(decision.reason, "timeout_advancing");
}

TEST_F(AIControlAdapterCombatTaskTests, Cohesion_FailsWhenBelowMinimumViable)
{
	CombatTaskCohesionDecision decision = evaluateCombatTaskCohesion({
		2,
		2,
		0,
		0,
		3,
		true
	});

	EXPECT_FALSE(decision.shouldAdvance);
	EXPECT_TRUE(decision.shouldFail);
	EXPECT_STREQ(decision.reason, "below_minimum_viable");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbePolicy_HoldsWhenCohesionDelayIsShort)
{
	CombatTaskProbeDecision decision = evaluateCombatTaskProbePolicy({
		true,
		false,
		12000,
		30000,
		0,
		CombatTaskProbeState::Inactive,
		0,
		20000,
		0,
		50000,
		15000,
		3
	});

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_STREQ(decision.reason, "cohesion_wait_below_threshold");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbePolicy_LaunchesAfterOverwaitWithoutFreshTargets)
{
	CombatTaskProbeDecision decision = evaluateCombatTaskProbePolicy({
		true,
		false,
		35000,
		30000,
		0,
		CombatTaskProbeState::Inactive,
		0,
		20000,
		0,
		70000,
		15000,
		2
	});

	EXPECT_TRUE(decision.shouldLaunch);
	EXPECT_STREQ(decision.mode, "launch");
	EXPECT_STREQ(decision.reason, "stale_enemy_memory_probe");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbePolicy_DoesNotLaunchWhenFreshStrategicTargetsExist)
{
	CombatTaskProbeDecision decision = evaluateCombatTaskProbePolicy({
		true,
		false,
		45000,
		30000,
		1,
		CombatTaskProbeState::Inactive,
		0,
		20000,
		0,
		90000,
		15000,
		3
	});

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_STREQ(decision.reason, "fresh_targets_available");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbeSelection_ExcludesReservedWorkersAndDefenseFloorUnits)
{
	std::vector<CombatTaskProbeCandidate> candidates;
	candidates.push_back(CombatTaskProbeCandidate{1, false, true, true, false, false, false, false, false, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{2, true, true, true, true, false, false, false, false, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{3, true, true, true, false, true, false, false, false, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{4, true, true, true, false, false, true, false, false, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{5, true, true, true, false, false, false, true, false, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{6, true, true, true, false, false, false, false, true, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{7, true, true, true, false, false, false, false, false, 100.0f});

	std::vector<unsigned int> selected = selectCombatTaskProbeUnits(candidates, 3, 1200.0f);

	ASSERT_EQ(selected.size(), 1u);
	EXPECT_EQ(selected[0], 7u);
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbeSelection_BoundsCountAndPrefersFastUnits)
{
	std::vector<CombatTaskProbeCandidate> candidates;
	candidates.push_back(CombatTaskProbeCandidate{10, true, false, true, false, false, false, false, false, 50.0f});
	candidates.push_back(CombatTaskProbeCandidate{11, true, true, true, false, false, false, false, false, 300.0f});
	candidates.push_back(CombatTaskProbeCandidate{12, true, true, true, false, false, false, false, false, 100.0f});
	candidates.push_back(CombatTaskProbeCandidate{13, true, true, true, false, false, false, false, false, 200.0f});
	candidates.push_back(CombatTaskProbeCandidate{14, true, true, true, false, false, false, false, false, 1300.0f});

	std::vector<unsigned int> selected = selectCombatTaskProbeUnits(candidates, 2, 1200.0f);

	ASSERT_EQ(selected.size(), 2u);
	EXPECT_EQ(selected[0], 12u);
	EXPECT_EQ(selected[1], 13u);
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbePolicy_CooldownBlocksAdditionalProbe)
{
	CombatTaskProbeDecision decision = evaluateCombatTaskProbePolicy({
		true,
		false,
		60000,
		30000,
		0,
		CombatTaskProbeState::Complete,
		0,
		20000,
		100000,
		110000,
		15000,
		3
	});

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_STREQ(decision.reason, "probe_cooldown");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbePolicy_TimesOutActiveProbe)
{
	CombatTaskProbeDecision decision = evaluateCombatTaskProbePolicy({
		true,
		false,
		70000,
		30000,
		0,
		CombatTaskProbeState::Scouting,
		100000,
		20000,
		0,
		121000,
		15000,
		0
	});

	EXPECT_TRUE(decision.shouldTimeout);
	EXPECT_STREQ(decision.mode, "timeout");
	EXPECT_STREQ(decision.reason, "probe_timeout");
}

TEST_F(AIControlAdapterCombatTaskTests, RaidProbePolicy_MainQuorumCancelsActiveProbe)
{
	CombatTaskProbeDecision decision = evaluateCombatTaskProbePolicy({
		false,
		true,
		0,
		30000,
		0,
		CombatTaskProbeState::Moving,
		100000,
		20000,
		0,
		110000,
		15000,
		0
	});

	EXPECT_TRUE(decision.shouldCancel);
	EXPECT_STREQ(decision.mode, "cancel");
	EXPECT_STREQ(decision.reason, "main_quorum_reached");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPolicy_ReadyScudsWithoutFreshTargetsLaunch)
{
	CombatTaskScoutPolicyDecision decision = evaluateCombatTaskScoutPolicy({
		13,
		0,
		0,
		2,
		2,
		0,
		1,
		false,
		false,
		0,
		0u
	});

	EXPECT_TRUE(decision.shouldLaunch);
	EXPECT_STREQ(decision.mode, "launch");
	EXPECT_STREQ(decision.reason, "scud_target_starved");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPolicy_FreshTargetsSuppressLaunchUnderPressure)
{
	CombatTaskScoutPolicyDecision decision = evaluateCombatTaskScoutPolicy({
		4,
		2,
		0,
		3,
		2,
		0,
		1,
		true,
		false,
		0,
		0u
	});

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_STREQ(decision.reason, "fresh_targets_combat_pressure");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPolicy_ActiveScoutCapBlocksLaunch)
{
	CombatTaskScoutPolicyDecision decision = evaluateCombatTaskScoutPolicy({
		13,
		0,
		4,
		4,
		2,
		1,
		1,
		false,
		true,
		4,
		200000u
	});

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_STREQ(decision.mode, "active");
	EXPECT_STREQ(decision.reason, "scud_refresh_active");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPolicy_ScudRefreshLaunchesDespiteNoRaid)
{
	CombatTaskScoutPolicyDecision decision = evaluateCombatTaskScoutPolicy({
		8,
		0,
		0,
		3,
		2,
		0,
		1,
		false,
		true,
		3,
		180000u
	});

	EXPECT_TRUE(decision.shouldLaunch);
	EXPECT_STREQ(decision.mode, "launch");
	EXPECT_STREQ(decision.reason, "scud_target_refresh_stale");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPolicy_ScudRefreshHoldsWhenRegionsSwept)
{
	CombatTaskScoutPolicyDecision decision = evaluateCombatTaskScoutPolicy({
		8,
		0,
		0,
		3,
		2,
		0,
		1,
		false,
		true,
		0,
		30000u
	});

	EXPECT_FALSE(decision.shouldLaunch);
	EXPECT_STREQ(decision.reason, "likely_regions_recently_swept");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutObjective_PrefersStaleStrategicStructure)
{
	CombatTaskScoutObjective likelyBase;
	likelyBase.objectiveId = 900002;
	likelyBase.position = Coord3D{4000.0f, 4000.0f, 0.0f};
	likelyBase.priority = 70;
	likelyBase.likelyBase = true;
	likelyBase.reason = "likely_enemy_base";

	CombatTaskScoutObjective staleWmd;
	staleWmd.objectiveId = 4123;
	staleWmd.position = Coord3D{4200.0f, 1800.0f, 0.0f};
	staleWmd.priority = 140;
	staleWmd.staleStructure = true;
	staleWmd.reason = "stale_enemy_structure";

	CombatTaskScoutObjective selected = selectCombatTaskScoutObjective({likelyBase, staleWmd});

	EXPECT_EQ(selected.objectiveId, 4123u);
	EXPECT_TRUE(selected.staleStructure);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutObjective_KnownTargetsBeatRandomReveal)
{
	CombatTaskScoutObjective randomReveal;
	randomReveal.objectiveId = 700001;
	randomReveal.position = Coord3D{2500.0f, 2500.0f, 0.0f};
	randomReveal.priority = 45;
	randomReveal.randomReveal = true;
	randomReveal.reason = "random_reveal";

	CombatTaskScoutObjective staleProduction;
	staleProduction.objectiveId = 5100;
	staleProduction.position = Coord3D{4200.0f, 1800.0f, 0.0f};
	staleProduction.priority = 110;
	staleProduction.staleStructure = true;
	staleProduction.reason = "stale_enemy_structure";

	CombatTaskScoutObjective selected = selectCombatTaskScoutObjective({randomReveal, staleProduction});

	EXPECT_EQ(selected.objectiveId, 5100u);
	EXPECT_FALSE(selected.randomReveal);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutRandomObjective_GeneratesWhenCoverageThin)
{
	CombatTaskScoutRandomOrigin origin;
	origin.zoneId = 101;
	origin.position = Coord3D{1000.0f, 1000.0f, 0.0f};
	origin.mainBase = false;

	CombatTaskScoutRandomDecision decision = selectCombatTaskRandomRevealObjective({
		std::vector<CombatTaskScoutRandomOrigin>{origin},
		std::vector<CombatTaskScoutRandomBarrier>{},
		1u,
		0u,
		0.0f,
		0.0f,
		5000.0f,
		5000.0f,
		1200.0f,
		true
	});

	EXPECT_TRUE(decision.selected);
	EXPECT_TRUE(decision.objective.randomReveal);
	EXPECT_EQ(decision.objective.originZoneId, 101u);
	EXPECT_GT(decision.candidateCount, 0);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutRandomObjective_AvoidsImmediateRepeat)
{
	CombatTaskScoutRandomOrigin origin;
	origin.zoneId = 101;
	origin.position = Coord3D{1000.0f, 1000.0f, 0.0f};

	CombatTaskScoutRandomDecision first = selectCombatTaskRandomRevealObjective({
		std::vector<CombatTaskScoutRandomOrigin>{origin},
		std::vector<CombatTaskScoutRandomBarrier>{},
		42u,
		0u,
		0.0f,
		0.0f,
		5000.0f,
		5000.0f,
		1200.0f,
		true
	});
	ASSERT_EQ(first.selected, true);

	CombatTaskScoutRandomDecision second = selectCombatTaskRandomRevealObjective({
		std::vector<CombatTaskScoutRandomOrigin>{origin},
		std::vector<CombatTaskScoutRandomBarrier>{},
		42u,
		first.objective.objectiveId,
		0.0f,
		0.0f,
		5000.0f,
		5000.0f,
		1200.0f,
		true
	});

	EXPECT_TRUE(second.selected);
	EXPECT_NE(second.objective.objectiveId, first.objective.objectiveId);
	EXPECT_GT(second.rejectedCount, 0);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutRandomObjective_ClampsToMapBounds)
{
	CombatTaskScoutRandomOrigin origin;
	origin.zoneId = 7;
	origin.position = Coord3D{4900.0f, 4900.0f, 0.0f};

	CombatTaskScoutRandomDecision decision = selectCombatTaskRandomRevealObjective({
		std::vector<CombatTaskScoutRandomOrigin>{origin},
		std::vector<CombatTaskScoutRandomBarrier>{},
		0u,
		0u,
		0.0f,
		0.0f,
		5000.0f,
		5000.0f,
		2000.0f,
		true
	});

	EXPECT_TRUE(decision.selected);
	EXPECT_GE(decision.objective.position.x, 0.0f);
	EXPECT_LE(decision.objective.position.x, 5000.0f);
	EXPECT_GE(decision.objective.position.y, 0.0f);
	EXPECT_LE(decision.objective.position.y, 5000.0f);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutRandomObjective_RejectsImpassableBarrierCrossing)
{
	CombatTaskScoutRandomOrigin origin;
	origin.zoneId = 3;
	origin.position = Coord3D{1000.0f, 1000.0f, 0.0f};

	CombatTaskScoutRandomBarrier barrier;
	barrier.ax = 1500.0f;
	barrier.ay = 0.0f;
	barrier.bx = 1500.0f;
	barrier.by = 3000.0f;

	CombatTaskScoutRandomDecision decision = selectCombatTaskRandomRevealObjective({
		std::vector<CombatTaskScoutRandomOrigin>{origin},
		std::vector<CombatTaskScoutRandomBarrier>{barrier},
		0u,
		0u,
		0.0f,
		0.0f,
		5000.0f,
		5000.0f,
		1200.0f,
		true
	});

	EXPECT_TRUE(decision.selected);
	EXPECT_GT(decision.rejectedCount, 0);
	EXPECT_NE(decision.objective.position.x, 2200.0f);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutUnitSelection_ExcludesReservedAndProtectedUnits)
{
	std::vector<CombatTaskScoutCandidate> candidates;
	candidates.push_back(CombatTaskScoutCandidate{1, false, true, true, false, false, false, false, false, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{2, true, true, true, true, false, false, false, false, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{3, true, true, true, false, true, false, false, false, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{4, true, true, true, false, false, true, false, false, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{5, true, true, true, false, false, false, true, false, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{6, true, true, true, false, false, false, false, true, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{7, true, true, true, false, false, false, false, false, true, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{8, true, true, true, false, false, false, false, false, false, true, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{9, true, true, true, false, false, false, false, false, false, false, 0, 100.0f});

	std::vector<unsigned int> selected = selectCombatTaskScoutUnits(candidates, 2);

	ASSERT_EQ(selected.size(), 1u);
	EXPECT_EQ(selected[0], 9u);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutUnitSelection_RelaxedDefenseFloorStillExcludesTaskOwnedUnits)
{
	std::vector<CombatTaskScoutCandidate> candidates;
	candidates.push_back(CombatTaskScoutCandidate{40, true, true, true, false, false, false, false, true, false, false, 100, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{41, true, true, true, false, false, false, true, true, false, false, 100, 75.0f});
	candidates.push_back(CombatTaskScoutCandidate{42, true, true, true, false, false, false, false, false, true, false, 100, 50.0f});
	candidates.push_back(CombatTaskScoutCandidate{43, true, true, true, false, false, false, false, false, false, false, 60, 25.0f});

	std::vector<unsigned int> selected = selectCombatTaskScoutUnits(candidates, 3, true);

	ASSERT_EQ(selected.size(), 2u);
	EXPECT_EQ(selected[0], 40u);
	EXPECT_EQ(selected[1], 43u);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutUnitSelection_ExcludesWorkerShuttleReservedTechnicals)
{
	CombatTaskScoutCandidate reservedTechnical;
	reservedTechnical.unitId = 50;
	reservedTechnical.alive = true;
	reservedTechnical.fast = true;
	reservedTechnical.combatCapable = true;
	reservedTechnical.preference = 100;
	reservedTechnical.distanceFromOrigin = 25.0f;
	reservedTechnical.workerShuttleReserved = true;

	CombatTaskScoutCandidate availableQuad;
	availableQuad.unitId = 51;
	availableQuad.alive = true;
	availableQuad.fast = true;
	availableQuad.combatCapable = true;
	availableQuad.preference = 60;
	availableQuad.distanceFromOrigin = 50.0f;

	std::vector<CombatTaskScoutCandidate> candidates;
	candidates.push_back(reservedTechnical);
	candidates.push_back(availableQuad);

	std::vector<unsigned int> selected = selectCombatTaskScoutUnits(candidates, 2);

	ASSERT_EQ(selected.size(), 1u);
	EXPECT_EQ(selected[0], 51u);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutUnitSelection_BoundsCountAndPrefersFastUnits)
{
	std::vector<CombatTaskScoutCandidate> candidates;
	candidates.push_back(CombatTaskScoutCandidate{20, true, false, true, false, false, false, false, false, false, false, 0, 50.0f});
	candidates.push_back(CombatTaskScoutCandidate{21, true, true, true, false, false, false, false, false, false, false, 0, 300.0f});
	candidates.push_back(CombatTaskScoutCandidate{22, true, true, true, false, false, false, false, false, false, false, 0, 100.0f});
	candidates.push_back(CombatTaskScoutCandidate{23, true, true, true, false, false, false, false, false, false, false, 0, 200.0f});

	std::vector<unsigned int> selected = selectCombatTaskScoutUnits(candidates, 2);

	ASSERT_EQ(selected.size(), 2u);
	EXPECT_EQ(selected[0], 22u);
	EXPECT_EQ(selected[1], 23u);
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutUnitSelection_PrefersTechnicalsBeforeFallbacks)
{
	std::vector<CombatTaskScoutCandidate> candidates;
	candidates.push_back(CombatTaskScoutCandidate{30, true, true, true, false, false, false, false, false, false, false, 20, 50.0f});
	candidates.push_back(CombatTaskScoutCandidate{31, true, true, true, false, false, false, false, false, false, false, 60, 75.0f});
	candidates.push_back(CombatTaskScoutCandidate{32, true, true, true, false, false, false, false, false, false, false, 100, 400.0f});

	std::vector<unsigned int> selected = selectCombatTaskScoutUnits(candidates, 2);

	ASSERT_EQ(selected.size(), 2u);
	EXPECT_EQ(selected[0], 32u);
	EXPECT_EQ(selected[1], 31u);
}

TEST_F(AIControlAdapterCombatTaskTests, ScudTargetRefreshOverride_BorrowsWhenDefenseFloorBlocksHighCashRefresh)
{
	CombatTaskScudTargetRefreshOverrideDecision decision = evaluateCombatTaskScudTargetRefreshOverride({
		true,
		true,
		true,
		false,
		false,
		true,
		50000u,
		10000u,
		0,
		2,
		2,
		2,
		2
	});

	EXPECT_TRUE(decision.allowBorrow);
	EXPECT_TRUE(decision.relaxDefenseFloor);
	EXPECT_STREQ(decision.mode, "borrow");
	EXPECT_STREQ(decision.reason, "production_queued");
}

TEST_F(AIControlAdapterCombatTaskTests, ScudTargetRefreshOverride_ProducesWhenPoolSaturatedAndNothingQueued)
{
	CombatTaskScudTargetRefreshOverrideDecision decision = evaluateCombatTaskScudTargetRefreshOverride({
		true,
		true,
		true,
		false,
		false,
		true,
		50000u,
		10000u,
		0,
		2,
		2,
		0,
		2
	});

	EXPECT_TRUE(decision.allowProduction);
	EXPECT_FALSE(decision.allowBorrow);
	EXPECT_STREQ(decision.mode, "produce");
	EXPECT_STREQ(decision.reason, "defense_floor_relaxed");
}

TEST_F(AIControlAdapterCombatTaskTests, ScudTargetRefreshOverride_MainBaseCriticalBlocksBorrow)
{
	CombatTaskScudTargetRefreshOverrideDecision decision = evaluateCombatTaskScudTargetRefreshOverride({
		true,
		true,
		true,
		true,
		false,
		true,
		50000u,
		10000u,
		0,
		2,
		2,
		2,
		2
	});

	EXPECT_FALSE(decision.allowBorrow);
	EXPECT_FALSE(decision.allowProduction);
	EXPECT_STREQ(decision.reason, "critical_defense");
}

TEST_F(AIControlAdapterCombatTaskTests, ScudTargetRefreshOverride_CriticalZoneBlocksBorrow)
{
	CombatTaskScudTargetRefreshOverrideDecision decision = evaluateCombatTaskScudTargetRefreshOverride({
		true,
		true,
		true,
		false,
		true,
		true,
		50000u,
		10000u,
		0,
		2,
		2,
		2,
		2
	});

	EXPECT_FALSE(decision.allowBorrow);
	EXPECT_FALSE(decision.allowProduction);
	EXPECT_STREQ(decision.reason, "critical_zone_defense");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPool_BaselineMaintainsTwoTechnicals)
{
	CombatTaskScoutPoolDecision decision = evaluateCombatTaskScoutPool({
		true,
		true,
		false,
		5000u,
		1000u,
		0,
		0,
		0
	});

	EXPECT_TRUE(decision.productionNeeded);
	EXPECT_EQ(decision.desiredTechnicals, 2);
	EXPECT_STREQ(decision.unitTemplate, "GLAVehicleTechnical");
	EXPECT_STREQ(decision.reason, "scud_target_starved");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPool_HighCashCapsAtFour)
{
	CombatTaskScoutPoolDecision decision = evaluateCombatTaskScoutPool({
		true,
		true,
		false,
		22000u,
		10000u,
		2,
		0,
		0
	});

	EXPECT_TRUE(decision.productionNeeded);
	EXPECT_EQ(decision.desiredTechnicals, 4);
	EXPECT_STREQ(decision.reason, "scud_target_starved_high_cash");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPool_ExistingAndQueuedCountTowardPool)
{
	CombatTaskScoutPoolDecision decision = evaluateCombatTaskScoutPool({
		true,
		true,
		false,
		6000u,
		1000u,
		1,
		1,
		0
	});

	EXPECT_FALSE(decision.productionNeeded);
	EXPECT_EQ(decision.desiredTechnicals, 2);
	EXPECT_STREQ(decision.reason, "scout_pool_satisfied");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutPool_FreshCoverageSuppressesProduction)
{
	CombatTaskScoutPoolDecision decision = evaluateCombatTaskScoutPool({
		true,
		false,
		true,
		50000u,
		10000u,
		0,
		0,
		0
	});

	EXPECT_FALSE(decision.productionNeeded);
	EXPECT_EQ(decision.desiredTechnicals, 0);
	EXPECT_STREQ(decision.reason, "coverage_fresh");
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutTaskReservation_ReleasesOnTimeoutOrCompletion)
{
	const unsigned int taskId = manager.createTask(
		CombatTaskType::Scout,
		std::vector<unsigned int>{301, 302},
		Coord3D{4000.0f, 4000.0f, 0.0f},
		"dedicated_scouting",
		"scud_target_starved",
		45000);

	EXPECT_TRUE(manager.isUnitReserved(301));
	manager.expireTask(taskId, "scout_timeout");
	EXPECT_FALSE(manager.isUnitReserved(301));

	const unsigned int taskId2 = manager.createTask(
		CombatTaskType::Scout,
		std::vector<unsigned int>{401},
		Coord3D{4200.0f, 1800.0f, 0.0f},
		"dedicated_scouting",
		"stale_enemy_memory",
		45000);
	EXPECT_TRUE(manager.isUnitReserved(401));
	manager.completeTask(taskId2, "fresh_target_revealed");
	EXPECT_FALSE(manager.isUnitReserved(401));
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutStateLogThrottle_SuppressesUnchangedUntilHeartbeat)
{
	CombatTask task;
	task.type = CombatTaskType::Scout;

	EXPECT_TRUE(shouldLogCombatTaskScoutState(task, 1000, "scouting", "enroute", 2, 0, 4000));
	EXPECT_FALSE(shouldLogCombatTaskScoutState(task, 2000, "scouting", "enroute", 2, 0, 4000));
	EXPECT_FALSE(shouldLogCombatTaskScoutState(task, 4999, "scouting", "enroute", 2, 0, 4000));
	EXPECT_TRUE(shouldLogCombatTaskScoutState(task, 5000, "scouting", "enroute", 2, 0, 4000));
}

TEST_F(AIControlAdapterCombatTaskTests, ScoutStateLogThrottle_LogsMeaningfulChanges)
{
	CombatTask task;
	task.type = CombatTaskType::Scout;

	EXPECT_TRUE(shouldLogCombatTaskScoutState(task, 1000, "scouting", "enroute", 1, 0, 4000));
	EXPECT_TRUE(shouldLogCombatTaskScoutState(task, 1200, "scouting", "enroute", 2, 0, 4000));
	EXPECT_TRUE(shouldLogCombatTaskScoutState(task, 1400, "moving", "refresh_scout_waypoint", 2, 0, 4000));
	EXPECT_TRUE(shouldLogCombatTaskScoutState(task, 1600, "moving", "refresh_scout_waypoint", 2, 1, 4000));
	EXPECT_FALSE(shouldLogCombatTaskScoutState(task, 1800, "moving", "refresh_scout_waypoint", 2, 1, 4000));
}

TEST_F(AIControlAdapterCombatTaskTests, CohesionLogThrottle_SuppressesUnchangedStateBeforeHeartbeat)
{
	CombatTask task;
	task.state = CombatTaskState::WaitingForCohesion;
	task.cohesionReason = "waiting_for_infantry_quorum";
	task.currentWaypointIndex = 0;
	task.arrivedCount = 7;
	task.missingCount = 21;
	task.confirmedDeadCount = 0;

	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 1000, 4000, 3));
	EXPECT_FALSE(shouldLogCombatTaskCohesion(task, 2000, 4000, 3));
	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 5000, 4000, 3));
}

TEST_F(AIControlAdapterCombatTaskTests, CohesionLogThrottle_LogsStateReasonWaypointAndCountChanges)
{
	CombatTask task;
	task.state = CombatTaskState::WaitingForCohesion;
	task.cohesionReason = "waiting_for_infantry_quorum";
	task.currentWaypointIndex = 0;
	task.arrivedCount = 7;
	task.missingCount = 21;
	task.confirmedDeadCount = 0;

	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 1000, 4000, 3));
	task.state = CombatTaskState::MovingToStage;
	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 1200, 4000, 3));
	task.state = CombatTaskState::WaitingForCohesion;
	task.cohesionReason = "waiting_for_cohesion";
	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 1400, 4000, 3));
	task.currentWaypointIndex = 1;
	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 1600, 4000, 3));
	task.arrivedCount += 3;
	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 1800, 4000, 3));
	task.confirmedDeadCount += 1;
	EXPECT_TRUE(shouldLogCombatTaskCohesion(task, 2000, 4000, 3));
}

int main()
{
	for (const TestCase& test : testCases())
	{
		manager = AIControlAdapterCombatTaskManager();
		test.fn();
	}

	std::cout << "AIControlAdapterCombatTaskTests passed: " << testCases().size() << "\n";
	return 0;
}
