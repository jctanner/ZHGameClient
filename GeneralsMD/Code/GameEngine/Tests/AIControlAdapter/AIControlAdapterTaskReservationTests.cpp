#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTaskReservation.h"
#include <gtest/gtest.h>

//==============================================================================
// Phase 6.1: Special Operations Task Reservations Tests
//==============================================================================

class AIControlAdapterTaskReservationTests : public ::testing::Test
{
protected:
	AIControlAdapterTaskReservationManager manager;
};

TEST_F(AIControlAdapterTaskReservationTests, CreateReservation_CreatesTaskWithUniqueId)
{
	const unsigned int taskId1 = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	const unsigned int taskId2 = manager.createReservation(
		SpecialTaskType::BuildStructure,
		101,
		"GLABlackMarket",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_GT(taskId1, 0u);
	EXPECT_GT(taskId2, 0u);
	EXPECT_NE(taskId1, taskId2);
}

TEST_F(AIControlAdapterTaskReservationTests, CreateReservation_InitializesStateToAssigned)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Assigned);
	EXPECT_EQ(task->type, SpecialTaskType::BuildStructure);
	EXPECT_EQ(task->sourceObjectId, 100u);
	EXPECT_EQ(task->expectedTemplate, "GLASupplyStash");
	EXPECT_EQ(task->owner, "smart_build");
}

TEST_F(AIControlAdapterTaskReservationTests, IsObjectReserved_ReturnsTrueForReservedSource)
{
	manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_TRUE(manager.isObjectReserved(100));
	EXPECT_FALSE(manager.isObjectReserved(101));
}

TEST_F(AIControlAdapterTaskReservationTests, IsObjectReserved_ReturnsFalseForCompletedTask)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_TRUE(manager.isObjectReserved(100));

	manager.completeTask(taskId, "structure_finished");

	EXPECT_FALSE(manager.isObjectReserved(100));
}

TEST_F(AIControlAdapterTaskReservationTests, IsObjectReserved_ReturnsFalseForFailedTask)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_TRUE(manager.isObjectReserved(100));

	manager.failTask(taskId, "worker_dead");

	EXPECT_FALSE(manager.isObjectReserved(100));
}

TEST_F(AIControlAdapterTaskReservationTests, IsObjectReserved_ReturnsFalseForExpiredTask)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_TRUE(manager.isObjectReserved(100));

	manager.expireTask(taskId, "timeout");

	EXPECT_FALSE(manager.isObjectReserved(100));
}

TEST_F(AIControlAdapterTaskReservationTests, CanUseObjectForTask_AllowsSameOwner)
{
	manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_TRUE(manager.canUseObjectForTask(100, "smart_build"));
	EXPECT_FALSE(manager.canUseObjectForTask(100, "capture"));
}

TEST_F(AIControlAdapterTaskReservationTests, CanUseObjectForTask_AllowsUnreservedObject)
{
	EXPECT_TRUE(manager.canUseObjectForTask(100, "smart_build"));
	EXPECT_TRUE(manager.canUseObjectForTask(100, "capture"));
}

TEST_F(AIControlAdapterTaskReservationTests, UpdateTaskState_ChangesState)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Assigned);

	manager.updateTaskState(taskId, SpecialTaskState::Executing, "foundation_found");

	task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Executing);
	EXPECT_EQ(task->reason, "foundation_found");
}

TEST_F(AIControlAdapterTaskReservationTests, SetTaskTargetObject_UpdatesTargetObjectId)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->targetObjectId, 0u);

	manager.setTaskTargetObject(taskId, 500);

	task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->targetObjectId, 500u);
}

TEST_F(AIControlAdapterTaskReservationTests, FindReservationBySource_ReturnsCorrectTask)
{
	const unsigned int taskId = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	SpecialTaskReservation* task = manager.findReservationBySource(100);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->taskId, taskId);
	EXPECT_EQ(task->sourceObjectId, 100u);

	SpecialTaskReservation* notFound = manager.findReservationBySource(999);
	EXPECT_EQ(notFound, nullptr);
}

TEST_F(AIControlAdapterTaskReservationTests, FindBuildTasks_ReturnsOnlyBuildTasks)
{
	manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	manager.createCaptureReservation(101, 200, "capture", 60000);

	manager.createReservation(
		SpecialTaskType::BuildStructure,
		102,
		"GLABlackMarket",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);

	std::vector<SpecialTaskReservation*> buildTasks = manager.findBuildTasks();
	EXPECT_EQ(buildTasks.size(), 2u);

	for (SpecialTaskReservation* task : buildTasks)
	{
		EXPECT_EQ(task->type, SpecialTaskType::BuildStructure);
	}
}

TEST_F(AIControlAdapterTaskReservationTests, FindBuildTasks_FiltersBy Template)
{
	manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	manager.createReservation(
		SpecialTaskType::BuildStructure,
		101,
		"GLABlackMarket",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);

	manager.createReservation(
		SpecialTaskType::BuildStructure,
		102,
		"GLABlackMarket",
		Coord3D{1200.0f, 2200.0f, 0.0f},
		"smart_build",
		90000);

	std::vector<SpecialTaskReservation*> marketTasks = manager.findBuildTasks("BlackMarket");
	EXPECT_EQ(marketTasks.size(), 2u);

	for (SpecialTaskReservation* task : marketTasks)
	{
		EXPECT_NE(task->expectedTemplate.find("BlackMarket"), std::string::npos);
	}
}

TEST_F(AIControlAdapterTaskReservationTests, FindBuildTasks_ExcludesTerminalBuildTasks)
{
	const unsigned int activeTask = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLAScudStorm",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	const unsigned int stoppedTask = manager.createReservation(
		SpecialTaskType::BuildStructure,
		101,
		"GLAScudStorm",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);
	manager.failTask(stoppedTask, "stopped_stale_no_progress");

	std::vector<SpecialTaskReservation*> scudTasks = manager.findBuildTasks("ScudStorm");
	EXPECT_EQ(scudTasks.size(), 1u);
	ASSERT_NE(scudTasks[0], nullptr);
	EXPECT_EQ(scudTasks[0]->taskId, activeTask);
}

TEST_F(AIControlAdapterTaskReservationTests, StoppedFoundationTombstone_BlocksOrphanReadoptionDuringCooldown)
{
	manager.tombstoneStoppedFoundation(
		2815,
		"GLAScudStorm",
		Coord3D{1398.5f, 4673.1f, 0.0f},
		"stopped_stale_no_progress",
		1000,
		120000);

	DWORD ageMs = 0;
	std::string reason;
	EXPECT_TRUE(manager.isFoundationTombstoned(2815, 17000, &ageMs, &reason));
	EXPECT_EQ(ageMs, 16000u);
	EXPECT_EQ(reason, "stopped_stale_no_progress");
}

TEST_F(AIControlAdapterTaskReservationTests, StoppedFoundationTombstone_SkipLogIsThrottled)
{
	manager.tombstoneStoppedFoundation(
		2815,
		"GLAScudStorm",
		Coord3D{1398.5f, 4673.1f, 0.0f},
		"stopped_stale_no_progress",
		1000,
		120000);

	EXPECT_TRUE(manager.shouldLogFoundationTombstoneSkip(2815, 17000, 5000));
	EXPECT_FALSE(manager.shouldLogFoundationTombstoneSkip(2815, 18000, 5000));
	EXPECT_TRUE(manager.shouldLogFoundationTombstoneSkip(2815, 22000, 5000));
}

TEST_F(AIControlAdapterTaskReservationTests, StoppedFoundationTombstone_ExpiresAndCanBeCleared)
{
	manager.tombstoneStoppedFoundation(
		2815,
		"GLAScudStorm",
		Coord3D{1398.5f, 4673.1f, 0.0f},
		"stopped_stale_no_progress",
		1000,
		120000);

	EXPECT_FALSE(manager.isFoundationTombstoned(2815, 121000));

	manager.tombstoneStoppedFoundation(
		2815,
		"GLAScudStorm",
		Coord3D{1398.5f, 4673.1f, 0.0f},
		"stopped_stale_no_progress",
		200000,
		120000);
	EXPECT_TRUE(manager.isFoundationTombstoned(2815, 201000));

	manager.clearFoundationTombstone(2815);
	EXPECT_FALSE(manager.isFoundationTombstoned(2815, 202000));
}

TEST_F(AIControlAdapterTaskReservationTests, FindActiveTasks_ExcludesTerminalStates)
{
	const unsigned int task1 = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	const unsigned int task2 = manager.createReservation(
		SpecialTaskType::BuildStructure,
		101,
		"GLABlackMarket",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);

	const unsigned int task3 = manager.createReservation(
		SpecialTaskType::BuildStructure,
		102,
		"GLABarracks",
		Coord3D{1200.0f, 2200.0f, 0.0f},
		"smart_build",
		90000);

	manager.completeTask(task2, "structure_finished");
	manager.failTask(task3, "worker_dead");

	std::vector<SpecialTaskReservation*> activeTasks = manager.findActiveTasks();
	EXPECT_EQ(activeTasks.size(), 1u);
	EXPECT_EQ(activeTasks[0]->taskId, task1);
}

TEST_F(AIControlAdapterTaskReservationTests, GetActiveTaskCount_CountsNonTerminalTasks)
{
	EXPECT_EQ(manager.getActiveTaskCount(), 0);

	const unsigned int task1 = manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_EQ(manager.getActiveTaskCount(), 1);

	manager.createReservation(
		SpecialTaskType::BuildStructure,
		101,
		"GLABlackMarket",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);

	EXPECT_EQ(manager.getActiveTaskCount(), 2);

	manager.completeTask(task1, "structure_finished");

	EXPECT_EQ(manager.getActiveTaskCount(), 1);
}

TEST_F(AIControlAdapterTaskReservationTests, CreateCaptureReservation_InitializesCaptureTask)
{
	const unsigned int taskId = manager.createCaptureReservation(100, 200, "capture", 60000);

	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->type, SpecialTaskType::CaptureStructure);
	EXPECT_EQ(task->state, SpecialTaskState::Assigned);
	EXPECT_EQ(task->sourceObjectId, 100u);
	EXPECT_EQ(task->targetObjectId, 200u);
	EXPECT_EQ(task->owner, "capture");
}

TEST_F(AIControlAdapterTaskReservationTests, IsObjectReserved_ReturnsTrueForCaptureTarget)
{
	manager.createCaptureReservation(100, 200, "capture", 60000);

	EXPECT_TRUE(manager.isObjectReserved(100)); // Source
	EXPECT_TRUE(manager.isObjectReserved(200)); // Target
	EXPECT_FALSE(manager.isObjectReserved(300)); // Unrelated
}

//==============================================================================
// Phase 6.2: Capturable Building Task Reliability Tests
//==============================================================================

TEST_F(AIControlAdapterTaskReservationTests, Phase62_CaptureSource_ExcludedFromCombatWhenReserved)
{
	// Create a capture task reserving unit 100
	const unsigned int taskId = manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Unit 100 should be reserved (not available for combat/guard)
	EXPECT_TRUE(manager.isObjectReserved(100));

	// Verify task is active
	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Assigned);
	EXPECT_EQ(task->type, SpecialTaskType::CaptureStructure);
	EXPECT_EQ(task->sourceObjectId, 100u);
	EXPECT_EQ(task->targetObjectId, 200u);

	// Complete the task
	manager.completeTask(taskId, "target_captured");

	// Now unit 100 should be available again
	EXPECT_FALSE(manager.isObjectReserved(100));
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_CaptureTarget_DuplicateTaskPrevention)
{
	// Create first capture task for target 200
	const unsigned int task1 = manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Verify first task created
	ASSERT_GT(task1, 0u);

	// Check if target 200 has an active capture task
	std::vector<SpecialTaskReservation*> captureTasks = manager.findCaptureTasks();
	bool hasActiveTaskForTarget200 = false;
	for (const SpecialTaskReservation* task : captureTasks)
	{
		if (task != nullptr &&
			task->targetObjectId == 200u &&
			task->state != SpecialTaskState::Complete &&
			task->state != SpecialTaskState::Failed &&
			task->state != SpecialTaskState::Expired)
		{
			hasActiveTaskForTarget200 = true;
			break;
		}
	}

	EXPECT_TRUE(hasActiveTaskForTarget200);

	// Creating another task for the same target should be prevented
	// (in actual code this check happens in collectCapturableTargetsForPlayer)
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_MissingSource_TaskCanBeMarkedFailed)
{
	const unsigned int taskId = manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Verify task is active
	EXPECT_TRUE(manager.isObjectReserved(100));

	// Mark task as failed due to missing source
	manager.failTask(taskId, "source_dead");

	// Source should no longer be reserved
	EXPECT_FALSE(manager.isObjectReserved(100));

	// Task should be in Failed state
	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Failed);
	EXPECT_EQ(task->reason, "source_dead");
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_MissingTarget_TaskCanBeMarkedFailed)
{
	const unsigned int taskId = manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Verify task is active
	EXPECT_TRUE(manager.isObjectReserved(100));
	EXPECT_TRUE(manager.isObjectReserved(200));

	// Mark task as failed due to missing target
	manager.failTask(taskId, "target_dead");

	// Both source and target should no longer be reserved
	EXPECT_FALSE(manager.isObjectReserved(100));
	EXPECT_FALSE(manager.isObjectReserved(200));

	// Task should be in Failed state
	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Failed);
	EXPECT_EQ(task->reason, "target_dead");
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_FriendlyTarget_TaskCanBeMarkedComplete)
{
	const unsigned int taskId = manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Verify task is active
	EXPECT_TRUE(manager.isObjectReserved(100));

	// Mark task as complete (target captured)
	manager.completeTask(taskId, "target_captured");

	// Source should no longer be reserved
	EXPECT_FALSE(manager.isObjectReserved(100));

	// Task should be in Complete state
	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Complete);
	EXPECT_EQ(task->reason, "target_captured");
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_ExpiredTask_ReleasesUnitsAndAllowsRetry)
{
	const unsigned int taskId = manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Verify task is active
	EXPECT_TRUE(manager.isObjectReserved(100));

	// Mark task as expired
	manager.expireTask(taskId, "timeout");

	// Source should no longer be reserved (can be reused for retry)
	EXPECT_FALSE(manager.isObjectReserved(100));

	// Task should be in Expired state
	const SpecialTaskReservation* task = manager.findReservation(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->state, SpecialTaskState::Expired);
	EXPECT_EQ(task->reason, "timeout");

	// Should be able to create a new task for the same target (retry)
	const unsigned int retryTaskId = manager.createCaptureReservation(101, 200, "capture_automation", 60000);
	ASSERT_GT(retryTaskId, 0u);
	ASSERT_NE(retryTaskId, taskId);
	EXPECT_TRUE(manager.isObjectReserved(101));
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_SameOwner_CanReuseReservedUnit)
{
	// Capture automation reserves unit 100
	manager.createCaptureReservation(100, 200, "capture_automation", 60000);

	// Same owner can check if they can use the unit (should be allowed)
	EXPECT_TRUE(manager.canUseObjectForTask(100, "capture_automation"));

	// Different owner cannot use the unit
	EXPECT_FALSE(manager.canUseObjectForTask(100, "idle_guard"));
	EXPECT_FALSE(manager.canUseObjectForTask(100, "attack_automation"));
	EXPECT_FALSE(manager.canUseObjectForTask(100, "zone_defense"));
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_FindCaptureTasks_ReturnsOnlyCaptureTasks)
{
	// Create mix of build and capture tasks
	manager.createReservation(
		SpecialTaskType::BuildStructure,
		100,
		"GLASupplyStash",
		Coord3D{1000.0f, 2000.0f, 0.0f},
		"smart_build",
		90000);

	manager.createCaptureReservation(101, 200, "capture_automation", 60000);
	manager.createCaptureReservation(102, 201, "capture_automation", 60000);

	manager.createReservation(
		SpecialTaskType::BuildStructure,
		103,
		"GLABlackMarket",
		Coord3D{1100.0f, 2100.0f, 0.0f},
		"smart_build",
		90000);

	// Find capture tasks
	std::vector<SpecialTaskReservation*> captureTasks = manager.findCaptureTasks();
	EXPECT_EQ(captureTasks.size(), 2u);

	for (SpecialTaskReservation* task : captureTasks)
	{
		EXPECT_EQ(task->type, SpecialTaskType::CaptureStructure);
	}
}

TEST_F(AIControlAdapterTaskReservationTests, Phase62_GetCaptureTaskCount_CountsOnlyActiveTasks)
{
	// Create multiple capture tasks
	const unsigned int task1 = manager.createCaptureReservation(100, 200, "capture_automation", 60000);
	const unsigned int task2 = manager.createCaptureReservation(101, 201, "capture_automation", 60000);
	const unsigned int task3 = manager.createCaptureReservation(102, 202, "capture_automation", 60000);

	// Should have 3 active capture tasks
	EXPECT_EQ(manager.getCaptureTaskCount(), 3);

	// Complete one task
	manager.completeTask(task1, "target_captured");
	EXPECT_EQ(manager.getCaptureTaskCount(), 2);

	// Fail another task
	manager.failTask(task2, "source_dead");
	EXPECT_EQ(manager.getCaptureTaskCount(), 1);

	// Expire the last task
	manager.expireTask(task3, "timeout");
	EXPECT_EQ(manager.getCaptureTaskCount(), 0);
}
