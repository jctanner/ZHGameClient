#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"
#include <gtest/gtest.h>

//==============================================================================
// Phase 9: Combat Task Ownership Tests
//==============================================================================

class AIControlAdapterCombatTaskTests : public ::testing::Test
{
protected:
	AIControlAdapterCombatTaskManager manager;
};

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
