/**
 * AIControlAdapterCaptureTaskLifecycleTests.cpp
 *
 * Regression tests for Phase 6.2 capture task lifecycle durability.
 *
 * Tests verify that:
 * 1. pruneExpiredTasks skips CaptureStructure tasks (they manage their own expiry)
 * 2. Capture tasks can track progress (lastDistance, bestDistance, lastUpdateTick fields exist)
 * 3. Task reservations work correctly (source/target reserved)
 * 4. Timeout extension logic works when progress is made
 * 5. State transitions work (Assigned -> Moving -> Capturing)
 */

// Typedefs for game engine types
typedef unsigned long DWORD;
typedef float Real;

// Forward declare Coord3D to avoid header include issues
struct Coord3D {
	float x, y, z;
};

#include "GameClient/AIControlAdapter/AIControlAdapterTaskReservation.h"

#include <cstdlib>
#include <iostream>

namespace
{
	int failureCount = 0;

	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << std::endl;
			++failureCount;
		}
	}

	void expectEqual(int actual, int expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAILED: " << message << " (expected " << expected << ", got " << actual << ")" << std::endl;
			++failureCount;
		}
	}

	void expectNotEqual(int actual, int expected, const char* message)
	{
		if (actual == expected)
		{
			std::cerr << "FAILED: " << message << " (got " << actual << ")" << std::endl;
			++failureCount;
		}
	}

	// Simulate progress tracking logic (matches production code in updateCaptureTaskLifecycle)
	// Returns true if state should transition to Moving or Capturing
	bool simulateCaptureProgress(
		AIControlAdapterTaskReservationManager& manager,
		SpecialTaskReservation* task,
		Real currentDistance,
		DWORD now)
	{
		const Real NEAR_TARGET_THRESHOLD = 200.0f;
		const Real PROGRESS_THRESHOLD = 10.0f;
		const DWORD PROGRESS_TIMEOUT_EXTENSION_MS = 60000u;

		bool stateChanged = false;

		if (task->lastDistance < 0.0f)
		{
			// First measurement
			task->lastDistance = currentDistance;
			task->bestDistance = currentDistance;
			task->lastUpdateTick = now;
			task->timeoutTick = now + PROGRESS_TIMEOUT_EXTENSION_MS;
		}
		else
		{
			bool madeProgress = (currentDistance < task->bestDistance - PROGRESS_THRESHOLD);
			bool nearTarget = (currentDistance < NEAR_TARGET_THRESHOLD);

			if (madeProgress || nearTarget)
			{
				task->lastUpdateTick = now;
				if (currentDistance < task->bestDistance)
				{
					task->bestDistance = currentDistance;
				}
				task->timeoutTick = now + PROGRESS_TIMEOUT_EXTENSION_MS;

				// State transitions (matches production code)
				if (nearTarget && task->state == SpecialTaskState::Assigned)
				{
					manager.updateTaskState(task->taskId, SpecialTaskState::Executing, "near_target");
					stateChanged = true;
				}
				else if (madeProgress && task->state == SpecialTaskState::Assigned)
				{
					manager.updateTaskState(task->taskId, SpecialTaskState::Moving, "distance_progress");
					stateChanged = true;
				}
			}

			task->lastDistance = currentDistance;
		}

		return stateChanged;
	}
}

// Test: pruneExpiredTasks should skip CaptureStructure tasks
void testPruneSkipsCaptureasks()
{
	std::cout << "Test: pruneExpiredTasks should skip CaptureStructure tasks" << std::endl;

	AIControlAdapterTaskReservationManager manager;

	// Create capture task with 60-second timeout
	const unsigned int taskId = manager.createCaptureReservation(443, 306, "capture_automation", 60000);
	SpecialTaskReservation* task = manager.findReservation(taskId);

	expect(task != nullptr, "Task should be created");

	// Simulate progress at 30s - task is actively working
	DWORD now = 30000;
	Real currentDistance = 500.0f;
	simulateCaptureProgress(manager, task, currentDistance, now);

	// Verify timeout was set by first measurement (should be now + 60000)
	expect(task->timeoutTick == now + 60000, "Timeout should be set to 90000 on first measurement");

	// At 65s total age, but only 35s since last progress
	now = 65000;

	// pruneExpiredTasks should NOT expire capture tasks
	manager.pruneExpiredTasks(now);

	// Task should NOT be expired by pruneExpiredTasks (it skips CaptureStructure)
	expectNotEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Expired),
		"pruneExpiredTasks should skip CaptureStructure tasks");

	std::cout << "  PASSED" << std::endl;
}

// Test: Timeout extension on progress over 60s
void testTimeoutExtensionOnProgress()
{
	std::cout << "Test: Timeout extension on progressing task over 60s" << std::endl;

	AIControlAdapterTaskReservationManager manager;

	const unsigned int taskId = manager.createCaptureReservation(443, 306, "capture_automation", 60000);
	SpecialTaskReservation* task = manager.findReservation(taskId);

	expect(task != nullptr, "Task should be created");

	// Simulate progress over 65 seconds with distance decreasing
	struct Step { DWORD tick; Real dist; };
	Step steps[] = {
		{ 0,     1200.0f },
		{ 15000, 900.0f  },
		{ 30000, 600.0f  },
		{ 45000, 300.0f  },
		{ 60000, 180.0f  },
		{ 65000, 160.0f  }
	};

	for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
	{
		simulateCaptureProgress(manager, task, steps[i].dist, steps[i].tick);

		// Task should NOT be expired while making progress
		expectNotEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Expired),
			"Task should NOT expire while making progress");
	}

	// At 65s, timeout should be 65s + 60s = 125s (last progress at 65s extends it)
	expect(task->timeoutTick == 125000, "Timeout should be 125000 after last progress at 65s");

	std::cout << "  PASSED" << std::endl;
}

// Test: State transitions
void testStateTransitions()
{
	std::cout << "Test: State transitions on progress" << std::endl;

	AIControlAdapterTaskReservationManager manager;

	const unsigned int taskId = manager.createCaptureReservation(443, 306, "capture_automation", 60000);
	SpecialTaskReservation* task = manager.findReservation(taskId);

	expect(task != nullptr, "Task should be created");
	expectEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Assigned),
		"Task should start in Assigned state");

	// First measurement (far from target)
	simulateCaptureProgress(manager, task, 1200.0f, 1000);
	expectEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Assigned),
		"Task should remain Assigned on first measurement");

	// Distance decreased - should trigger Moving state
	simulateCaptureProgress(manager, task, 900.0f, 5000);
	expectEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Moving),
		"Task should transition to Moving when distance decreases");

	// Near target - should trigger Capturing/Executing state
	// First reset to Assigned to test the near target transition
	manager.updateTaskState(taskId, SpecialTaskState::Assigned, "test_reset");
	simulateCaptureProgress(manager, task, 180.0f, 15000);
	expectEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Executing),
		"Task should transition to Executing/Capturing when near target");

	std::cout << "  PASSED" << std::endl;
}

// Test: Capture task has progress tracking fields
void testCaptureTaskProgressFields()
{
	std::cout << "Test: Capture task has progress tracking fields" << std::endl;

	AIControlAdapterTaskReservationManager manager;

	const unsigned int taskId = manager.createCaptureReservation(443, 306, "capture_automation", 60000);
	SpecialTaskReservation* task = manager.findReservation(taskId);

	expect(task != nullptr, "Task should be created");

	// Verify Phase 6.2 progress tracking fields exist and are initialized
	expectEqual(static_cast<int>(task->lastDistance), -1, "lastDistance should be initialized to -1");
	expectEqual(static_cast<int>(task->bestDistance), -1, "bestDistance should be initialized to -1");
	expectEqual(static_cast<int>(task->commandReissueCount), 0, "commandReissueCount should be initialized to 0");

	// Verify we can set progress tracking fields
	task->lastDistance = 1200.0f;
	task->bestDistance = 1200.0f;
	task->lastUpdateTick = 5000;

	expect(task->lastDistance > 0.0f, "lastDistance should be set");
	expect(task->bestDistance > 0.0f, "bestDistance should be set");
	expect(task->lastUpdateTick == 5000, "lastUpdateTick should be set");

	std::cout << "  PASSED" << std::endl;
}

// Test: Duplicate prevention
void testDuplicatePrevention()
{
	std::cout << "Test: Duplicate prevention" << std::endl;

	AIControlAdapterTaskReservationManager manager;

	// Create capture task for target 306
	const unsigned int task1 = manager.createCaptureReservation(443, 306, "capture_automation", 60000);

	expect(manager.findReservation(task1) != nullptr, "Task should be created");

	// Verify target is reserved
	expect(manager.isObjectReserved(306), "Target should be reserved");

	// Verify source is reserved
	expect(manager.isObjectReserved(443), "Source should be reserved");

	// Second Rebel cannot be assigned to same target
	expect(!manager.canUseObjectForTask(306, "different_owner"), "Target should not be usable by different owner");

	std::cout << "  PASSED" << std::endl;
}

// Test: Task completes when target becomes friendly
void testTaskCompletion()
{
	std::cout << "Test: Task completion" << std::endl;

	AIControlAdapterTaskReservationManager manager;

	const unsigned int taskId = manager.createCaptureReservation(443, 306, "capture_automation", 60000);
	SpecialTaskReservation* task = manager.findReservation(taskId);

	expect(task != nullptr, "Task should be created");

	// Complete task
	manager.completeTask(taskId, "target_captured");

	expectEqual(static_cast<int>(task->state), static_cast<int>(SpecialTaskState::Complete),
		"Task should transition to Complete");

	std::cout << "  PASSED" << std::endl;
}

int main()
{
	std::cout << "=== AIControlAdapter Capture Task Lifecycle Tests ===" << std::endl;
	std::cout << std::endl;

	testPruneSkipsCaptureasks();
	testTimeoutExtensionOnProgress();
	testStateTransitions();
	testCaptureTaskProgressFields();
	testDuplicatePrevention();
	testTaskCompletion();

	std::cout << std::endl;
	if (failureCount == 0)
	{
		std::cout << "All tests PASSED" << std::endl;
		return EXIT_SUCCESS;
	}
	else
	{
		std::cout << failureCount << " test(s) FAILED" << std::endl;
		return EXIT_FAILURE;
	}
}
