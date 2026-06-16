#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTaskReservation.h"
#include <algorithm>

AIControlAdapterTaskReservationManager::AIControlAdapterTaskReservationManager()
{
}

unsigned int AIControlAdapterTaskReservationManager::createReservation(
	SpecialTaskType type,
	unsigned int sourceObjectId,
	const std::string& expectedTemplate,
	const Coord3D& targetPosition,
	const std::string& owner,
	unsigned int timeoutMs)
{
	const unsigned int taskId = nextTaskId++;
	const DWORD now = ::GetTickCount();

	SpecialTaskReservation reservation;
	reservation.taskId = taskId;
	reservation.type = type;
	reservation.state = SpecialTaskState::Assigned;
	reservation.sourceObjectId = sourceObjectId;
	reservation.targetObjectId = 0;
	reservation.expectedTemplate = expectedTemplate;
	reservation.targetPosition = targetPosition;
	reservation.owner = owner;
	reservation.createdTick = now;
	reservation.lastSeenTick = now;
	reservation.lastUpdateTick = now;
	reservation.timeoutTick = now + timeoutMs;
	reservation.reason = "assigned";

	reservations[taskId] = reservation;
	return taskId;
}

unsigned int AIControlAdapterTaskReservationManager::createCaptureReservation(
	unsigned int sourceObjectId,
	unsigned int targetObjectId,
	const std::string& owner,
	unsigned int timeoutMs)
{
	const unsigned int taskId = nextTaskId++;
	const DWORD now = ::GetTickCount();

	SpecialTaskReservation reservation;
	reservation.taskId = taskId;
	reservation.type = SpecialTaskType::CaptureStructure;
	reservation.state = SpecialTaskState::Assigned;
	reservation.sourceObjectId = sourceObjectId;
	reservation.targetObjectId = targetObjectId;
	reservation.expectedTemplate = "";
	reservation.targetPosition = Coord3D{0.0f, 0.0f, 0.0f};
	reservation.owner = owner;
	reservation.createdTick = now;
	reservation.lastSeenTick = now;
	reservation.lastUpdateTick = now;
	reservation.timeoutTick = now + timeoutMs;
	reservation.reason = "assigned";
	// Phase 6.2: Initialize progress tracking
	reservation.lastDistance = -1.0f;
	reservation.bestDistance = -1.0f;
	reservation.lastCommandTick = now; // Initial command issued
	reservation.commandReissueCount = 0;

	reservations[taskId] = reservation;
	return taskId;
}

bool AIControlAdapterTaskReservationManager::isObjectReserved(unsigned int objectId) const
{
	for (const auto& pair : reservations)
	{
		const SpecialTaskReservation& task = pair.second;
		if (task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			if (task.sourceObjectId == objectId || task.targetObjectId == objectId)
			{
				return true;
			}
		}
	}
	return false;
}

bool AIControlAdapterTaskReservationManager::canUseObjectForTask(
	unsigned int objectId,
	const std::string& requestingOwner) const
{
	for (const auto& pair : reservations)
	{
		const SpecialTaskReservation& task = pair.second;
		if (task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			if (task.sourceObjectId == objectId || task.targetObjectId == objectId)
			{
				// Allow same owner to reuse
				if (task.owner == requestingOwner)
				{
					return true;
				}
				return false;
			}
		}
	}
	return true; // Not reserved
}

SpecialTaskReservation* AIControlAdapterTaskReservationManager::findReservation(unsigned int taskId)
{
	auto it = reservations.find(taskId);
	return it != reservations.end() ? &it->second : nullptr;
}

const SpecialTaskReservation* AIControlAdapterTaskReservationManager::findReservation(unsigned int taskId) const
{
	auto it = reservations.find(taskId);
	return it != reservations.end() ? &it->second : nullptr;
}

SpecialTaskReservation* AIControlAdapterTaskReservationManager::findReservationBySource(unsigned int sourceObjectId)
{
	for (auto& pair : reservations)
	{
		if (pair.second.sourceObjectId == sourceObjectId)
		{
			return &pair.second;
		}
	}
	return nullptr;
}

const SpecialTaskReservation* AIControlAdapterTaskReservationManager::findReservationBySource(unsigned int sourceObjectId) const
{
	for (const auto& pair : reservations)
	{
		if (pair.second.sourceObjectId == sourceObjectId)
		{
			return &pair.second;
		}
	}
	return nullptr;
}

bool AIControlAdapterTaskReservationManager::updateTaskState(
	unsigned int taskId,
	SpecialTaskState newState,
	const std::string& reason)
{
	SpecialTaskReservation* task = findReservation(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->state = newState;
	task->reason = reason;
	task->lastUpdateTick = ::GetTickCount();
	return true;
}

bool AIControlAdapterTaskReservationManager::setTaskTargetObject(unsigned int taskId, unsigned int targetObjectId)
{
	SpecialTaskReservation* task = findReservation(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->targetObjectId = targetObjectId;
	task->lastUpdateTick = ::GetTickCount();
	return true;
}

bool AIControlAdapterTaskReservationManager::completeTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, SpecialTaskState::Complete, reason);
}

bool AIControlAdapterTaskReservationManager::failTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, SpecialTaskState::Failed, reason);
}

bool AIControlAdapterTaskReservationManager::expireTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, SpecialTaskState::Expired, reason);
}

void AIControlAdapterTaskReservationManager::updateTasks(DWORD currentTick)
{
	// Update last seen tick for active tasks
	for (auto& pair : reservations)
	{
		SpecialTaskReservation& task = pair.second;
		if (task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			task.lastSeenTick = currentTick;
		}
	}
}

void AIControlAdapterTaskReservationManager::pruneExpiredTasks(DWORD currentTick)
{
	std::vector<unsigned int> toRemove;

	for (auto& pair : reservations)
	{
		SpecialTaskReservation& task = pair.second;

		// Remove already terminal tasks
		if (task.state == SpecialTaskState::Complete ||
			task.state == SpecialTaskState::Failed ||
			task.state == SpecialTaskState::Expired)
		{
			// Keep terminal tasks for a short window for telemetry
			if (currentTick > task.lastUpdateTick + 5000) // 5 seconds
			{
				toRemove.push_back(task.taskId);
			}
			continue;
		}

		// Expire tasks past timeout
		// Phase 6.2: Skip CaptureStructure tasks - they manage their own expiry in updateCaptureTaskLifecycle()
		if (task.type != SpecialTaskType::CaptureStructure && currentTick >= task.timeoutTick)
		{
			task.state = SpecialTaskState::Expired;
			task.reason = "timeout";
			task.lastUpdateTick = currentTick;
		}
	}

	for (unsigned int taskId : toRemove)
	{
		reservations.erase(taskId);
	}

	std::vector<unsigned int> tombstonesToRemove;
	for (const auto& pair : stoppedFoundationTombstones)
	{
		const StoppedFoundationTombstone& tombstone = pair.second;
		if (currentTick >= tombstone.expiresTick)
		{
			tombstonesToRemove.push_back(pair.first);
		}
	}
	for (unsigned int foundationObjectId : tombstonesToRemove)
	{
		stoppedFoundationTombstones.erase(foundationObjectId);
	}
}

void AIControlAdapterTaskReservationManager::removeTask(unsigned int taskId)
{
	reservations.erase(taskId);
}

void AIControlAdapterTaskReservationManager::tombstoneStoppedFoundation(
	unsigned int foundationObjectId,
	const std::string& expectedTemplate,
	const Coord3D& position,
	const std::string& reason,
	DWORD currentTick,
	unsigned int ttlMs)
{
	if (foundationObjectId == 0u)
	{
		return;
	}

	StoppedFoundationTombstone tombstone;
	tombstone.foundationObjectId = foundationObjectId;
	tombstone.expectedTemplate = expectedTemplate;
	tombstone.position = position;
	tombstone.reason = reason;
	tombstone.createdTick = currentTick;
	tombstone.expiresTick = currentTick + ttlMs;
	tombstone.lastSkipLogTick = 0u;
	stoppedFoundationTombstones[foundationObjectId] = tombstone;
}

bool AIControlAdapterTaskReservationManager::isFoundationTombstoned(
	unsigned int foundationObjectId,
	DWORD currentTick,
	DWORD* ageMs,
	std::string* reason) const
{
	auto it = stoppedFoundationTombstones.find(foundationObjectId);
	if (it == stoppedFoundationTombstones.end())
	{
		return false;
	}

	const StoppedFoundationTombstone& tombstone = it->second;
	if (currentTick >= tombstone.expiresTick)
	{
		return false;
	}

	if (ageMs != nullptr)
	{
		*ageMs = currentTick - tombstone.createdTick;
	}
	if (reason != nullptr)
	{
		*reason = tombstone.reason;
	}
	return true;
}

bool AIControlAdapterTaskReservationManager::shouldLogFoundationTombstoneSkip(
	unsigned int foundationObjectId,
	DWORD currentTick,
	unsigned int throttleMs)
{
	auto it = stoppedFoundationTombstones.find(foundationObjectId);
	if (it == stoppedFoundationTombstones.end())
	{
		return false;
	}

	StoppedFoundationTombstone& tombstone = it->second;
	if (currentTick >= tombstone.expiresTick)
	{
		return false;
	}
	if (tombstone.lastSkipLogTick != 0u && currentTick < tombstone.lastSkipLogTick + throttleMs)
	{
		return false;
	}

	tombstone.lastSkipLogTick = currentTick;
	return true;
}

void AIControlAdapterTaskReservationManager::clearFoundationTombstone(unsigned int foundationObjectId)
{
	stoppedFoundationTombstones.erase(foundationObjectId);
}

std::vector<SpecialTaskReservation*> AIControlAdapterTaskReservationManager::findBuildTasks(const std::string& templateFilter)
{
	std::vector<SpecialTaskReservation*> result;
	for (auto& pair : reservations)
	{
		SpecialTaskReservation& task = pair.second;
		if (task.type == SpecialTaskType::BuildStructure)
		{
			if (task.state == SpecialTaskState::Complete ||
				task.state == SpecialTaskState::Failed ||
				task.state == SpecialTaskState::Expired)
			{
				continue;
			}
			if (templateFilter.empty() || task.expectedTemplate.find(templateFilter) != std::string::npos)
			{
				result.push_back(&task);
			}
		}
	}
	return result;
}

std::vector<SpecialTaskReservation*> AIControlAdapterTaskReservationManager::findCaptureTasks()
{
	std::vector<SpecialTaskReservation*> result;
	for (auto& pair : reservations)
	{
		SpecialTaskReservation& task = pair.second;
		if (task.type == SpecialTaskType::CaptureStructure)
		{
			result.push_back(&task);
		}
	}
	return result;
}

std::vector<SpecialTaskReservation*> AIControlAdapterTaskReservationManager::findActiveTasks()
{
	std::vector<SpecialTaskReservation*> result;
	for (auto& pair : reservations)
	{
		SpecialTaskReservation& task = pair.second;
		if (task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

int AIControlAdapterTaskReservationManager::getActiveTaskCount() const
{
	int count = 0;
	for (const auto& pair : reservations)
	{
		const SpecialTaskReservation& task = pair.second;
		if (task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterTaskReservationManager::getBuildTaskCount() const
{
	int count = 0;
	for (const auto& pair : reservations)
	{
		const SpecialTaskReservation& task = pair.second;
		if (task.type == SpecialTaskType::BuildStructure &&
			task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterTaskReservationManager::getCaptureTaskCount() const
{
	int count = 0;
	for (const auto& pair : reservations)
	{
		const SpecialTaskReservation& task = pair.second;
		if (task.type == SpecialTaskType::CaptureStructure &&
			task.state != SpecialTaskState::Complete &&
			task.state != SpecialTaskState::Failed &&
			task.state != SpecialTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}
