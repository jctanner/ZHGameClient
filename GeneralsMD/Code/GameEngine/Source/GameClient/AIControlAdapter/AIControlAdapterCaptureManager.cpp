#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCaptureManager.h"

#include <algorithm>

AIControlAdapterCaptureProgressResult::AIControlAdapterCaptureProgressResult()
	: measured(false)
	, nearTarget(false)
	, loggedProgress(false)
	, progressReason("")
	, updateState(false)
	, newState(SpecialTaskState::Assigned)
	, stateReason("")
	, stateLogName("")
{
}

AIControlAdapterCaptureCommandDecision::AIControlAdapterCaptureCommandDecision()
	: shouldReissue(false)
	, reason("")
	, staleDurationMs(0u)
	, timeSinceLastCommandMs(0u)
{
}

AIControlAdapterCaptureExpirationDecision::AIControlAdapterCaptureExpirationDecision()
	: shouldExpire(false)
	, reason("")
	, staleDurationMs(0u)
{
}

AIControlAdapterCaptureAssignmentBudgetDecision::AIControlAdapterCaptureAssignmentBudgetDecision()
	: canAssign(false)
	, assignmentsRemaining(0)
	, attemptsRemaining(0)
	, reason("pending_limit")
{
}

bool AIControlAdapterCaptureManager::IsTerminalState(SpecialTaskState state)
{
	return state == SpecialTaskState::Complete ||
		state == SpecialTaskState::Failed ||
		state == SpecialTaskState::Expired;
}

const SpecialTaskReservation* AIControlAdapterCaptureManager::FindActiveCaptureReservationForObject(
	const std::vector<SpecialTaskReservation*>& captureTasks,
	unsigned int objectId)
{
	for (const SpecialTaskReservation* task : captureTasks)
	{
		if (task == nullptr || IsTerminalState(task->state))
		{
			continue;
		}
		if (task->sourceObjectId == objectId || task->targetObjectId == objectId)
		{
			return task;
		}
	}
	return nullptr;
}

bool AIControlAdapterCaptureManager::IsCaptureTargetReserved(
	const std::vector<SpecialTaskReservation*>& captureTasks,
	unsigned int targetObjectId)
{
	for (const SpecialTaskReservation* task : captureTasks)
	{
		if (task != nullptr &&
			!IsTerminalState(task->state) &&
			task->targetObjectId == targetObjectId)
		{
			return true;
		}
	}
	return false;
}

bool AIControlAdapterCaptureManager::IsCaptureSourceReserved(
	bool taskReserved,
	bool garrisonReserved)
{
	return taskReserved || garrisonReserved;
}

AIControlAdapterCaptureProgressResult AIControlAdapterCaptureManager::ApplyProgressMeasurement(
	SpecialTaskReservation& task,
	unsigned int now,
	Real currentDistance)
{
	AIControlAdapterCaptureProgressResult result;
	const Real nearTargetThreshold = 200.0f;
	const Real progressThreshold = 10.0f;
	const unsigned int progressTimeoutExtensionMs = 60000u;
	result.measured = true;
	result.nearTarget = (currentDistance <= nearTargetThreshold);

	if (task.lastDistance < 0.0f)
	{
		task.lastDistance = currentDistance;
		task.bestDistance = currentDistance;
		task.lastUpdateTick = now;
		task.timeoutTick = now + progressTimeoutExtensionMs;
		result.loggedProgress = true;
		result.progressReason = "first_measurement";

		if (result.nearTarget && task.state != SpecialTaskState::Executing)
		{
			result.updateState = true;
			result.newState = SpecialTaskState::Executing;
			result.stateReason = "near_target";
			result.stateLogName = "Capturing";
		}
		return result;
	}

	const bool madeProgress = (currentDistance < task.bestDistance - progressThreshold);
	const bool enteringNearTarget =
		result.nearTarget &&
		task.state != SpecialTaskState::Executing;

	if (madeProgress || enteringNearTarget)
	{
		task.lastUpdateTick = now;
		if (currentDistance < task.bestDistance)
		{
			task.bestDistance = currentDistance;
		}
		task.timeoutTick = now + progressTimeoutExtensionMs;
		result.loggedProgress = true;
		result.progressReason = madeProgress ? "distance_decreased" : "near_target";

		if (result.nearTarget && task.state != SpecialTaskState::Executing)
		{
			result.updateState = true;
			result.newState = SpecialTaskState::Executing;
			result.stateReason = "near_target";
			result.stateLogName = "Capturing";
		}
		else if (madeProgress && task.state == SpecialTaskState::Assigned)
		{
			result.updateState = true;
			result.newState = SpecialTaskState::Moving;
			result.stateReason = "distance_progress";
			result.stateLogName = "Moving";
		}
	}

	task.lastDistance = currentDistance;
	return result;
}

AIControlAdapterCaptureCommandDecision AIControlAdapterCaptureManager::ChooseCommandReissue(
	const SpecialTaskReservation& task,
	unsigned int now,
	Real currentDistance,
	bool nearTarget)
{
	AIControlAdapterCaptureCommandDecision decision;
	const unsigned int reissueThresholdMs = 10000u;
	const unsigned int nearTargetReissueMs = 8000u;
	const unsigned int staleThresholdMs = 30000u;
	const int maxReissues = 4;

	decision.staleDurationMs = now - task.lastUpdateTick;
	decision.timeSinceLastCommandMs = now - task.lastCommandTick;

	const bool canReissue =
		task.commandReissueCount < maxReissues &&
		currentDistance > 0.0f;
	const bool shouldReissueNearTarget =
		nearTarget &&
		decision.timeSinceLastCommandMs > nearTargetReissueMs &&
		decision.staleDurationMs > nearTargetReissueMs;
	const bool shouldReissueFarTarget =
		!nearTarget &&
		decision.staleDurationMs > reissueThresholdMs &&
		decision.staleDurationMs < staleThresholdMs &&
		decision.timeSinceLastCommandMs > reissueThresholdMs;

	if (canReissue && shouldReissueNearTarget)
	{
		decision.shouldReissue = true;
		decision.reason = "near_target_retry";
	}
	else if (canReissue && shouldReissueFarTarget)
	{
		decision.shouldReissue = true;
		decision.reason = "stalled_recovery";
	}
	return decision;
}

AIControlAdapterCaptureAssignmentBudgetDecision AIControlAdapterCaptureManager::ChooseAssignmentBudget(
	int pendingCount,
	int maxConcurrent,
	int attemptsPerAssignment)
{
	AIControlAdapterCaptureAssignmentBudgetDecision decision;
	if (maxConcurrent <= 0)
	{
		decision.reason = "disabled";
		return decision;
	}
	decision.assignmentsRemaining = std::max(0, maxConcurrent - std::max(0, pendingCount));
	decision.canAssign = decision.assignmentsRemaining > 0;
	decision.attemptsRemaining = decision.canAssign
		? std::max(decision.assignmentsRemaining, 1) * std::max(1, attemptsPerAssignment)
		: 0;
	decision.reason = decision.canAssign ? "available" : "pending_limit";
	return decision;
}

void AIControlAdapterCaptureManager::ApplyCommandReissueSuccess(
	SpecialTaskReservation& task,
	unsigned int now,
	unsigned int timeoutExtensionMs)
{
	task.lastCommandTick = now;
	task.commandReissueCount++;
	task.timeoutTick = now + timeoutExtensionMs;
}

AIControlAdapterCaptureExpirationDecision AIControlAdapterCaptureManager::ChooseExpiration(
	const SpecialTaskReservation& task,
	unsigned int now,
	Real currentDistance,
	bool nearTarget)
{
	AIControlAdapterCaptureExpirationDecision decision;
	const unsigned int staleThresholdMs = 30000u;
	const unsigned int nearTargetStaleMs = 45000u;
	const Real farFromTarget = 200.0f;
	const int maxReissues = 4;

	decision.staleDurationMs = now - task.lastUpdateTick;
	const bool isFarStalled = (decision.staleDurationMs > staleThresholdMs) &&
		(currentDistance < 0.0f || currentDistance > farFromTarget);
	const bool isNearStalled = nearTarget &&
		(decision.staleDurationMs > nearTargetStaleMs) &&
		(task.commandReissueCount >= maxReissues);
	const bool overallTimeout = (now >= task.timeoutTick);

	if ((isFarStalled || isNearStalled) && overallTimeout)
	{
		decision.shouldExpire = true;
		decision.reason = isNearStalled ? "near_target_no_capture" : "stalled_no_progress";
	}
	return decision;
}
