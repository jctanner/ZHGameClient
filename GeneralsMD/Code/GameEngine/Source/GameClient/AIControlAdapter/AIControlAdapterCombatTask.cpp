#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"
#include <algorithm>

AIControlAdapterCombatTaskManager::AIControlAdapterCombatTaskManager()
{
}

unsigned int AIControlAdapterCombatTaskManager::createTask(
	CombatTaskType type,
	const std::vector<unsigned int>& unitIds,
	const Coord3D& targetPosition,
	const std::string& owner,
	const std::string& reason,
	unsigned int timeoutMs)
{
	const unsigned int taskId = nextTaskId++;
	const DWORD now = ::GetTickCount();

	CombatTask task;
	task.taskId = taskId;
	task.type = type;
	task.state = CombatTaskState::Assembling;
	task.assignedUnitIds = unitIds;
	task.targetPosition = targetPosition;
	task.targetObjectId = 0;
	task.owner = owner;
	task.reason = reason;
	task.createdTick = now;
	task.lastCommandTick = now;
	task.lastProgressTick = now;
	task.timeoutTick = now + timeoutMs;
	task.initialUnitCount = static_cast<int>(unitIds.size());
	// Set minimum viable count to 30% of initial, at least 1
	task.minimumViableCount = std::max(1, task.initialUnitCount * 30 / 100);

	tasks[taskId] = task;
	return taskId;
}

bool AIControlAdapterCombatTaskManager::isUnitReserved(unsigned int unitId) const
{
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				return true;
			}
		}
	}
	return false;
}

bool AIControlAdapterCombatTaskManager::canUseUnitForTask(
	unsigned int unitId,
	const std::string& requestingOwner) const
{
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
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

CombatTask* AIControlAdapterCombatTaskManager::findTask(unsigned int taskId)
{
	auto it = tasks.find(taskId);
	return it != tasks.end() ? &it->second : nullptr;
}

const CombatTask* AIControlAdapterCombatTaskManager::findTask(unsigned int taskId) const
{
	auto it = tasks.find(taskId);
	return it != tasks.end() ? &it->second : nullptr;
}

CombatTask* AIControlAdapterCombatTaskManager::findTaskByUnit(unsigned int unitId)
{
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				return &task;
			}
		}
	}
	return nullptr;
}

const CombatTask* AIControlAdapterCombatTaskManager::findTaskByUnit(unsigned int unitId) const
{
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			auto it = std::find(task.assignedUnitIds.begin(), task.assignedUnitIds.end(), unitId);
			if (it != task.assignedUnitIds.end())
			{
				return &task;
			}
		}
	}
	return nullptr;
}

bool AIControlAdapterCombatTaskManager::updateTaskState(
	unsigned int taskId,
	CombatTaskState newState,
	const std::string& reason)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->state = newState;
	task->reason = reason;
	return true;
}

bool AIControlAdapterCombatTaskManager::updateTaskCommand(
	unsigned int taskId,
	DWORD currentTick)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->lastCommandTick = currentTick;
	return true;
}

bool AIControlAdapterCombatTaskManager::updateTaskProgress(
	unsigned int taskId,
	DWORD currentTick)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->lastProgressTick = currentTick;
	return true;
}

bool AIControlAdapterCombatTaskManager::completeTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, CombatTaskState::Complete, reason);
}

bool AIControlAdapterCombatTaskManager::failTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, CombatTaskState::Failed, reason);
}

bool AIControlAdapterCombatTaskManager::expireTask(unsigned int taskId, const std::string& reason)
{
	return updateTaskState(taskId, CombatTaskState::Expired, reason);
}

bool AIControlAdapterCombatTaskManager::removeDeadUnits(
	unsigned int taskId,
	const std::vector<unsigned int>& deadUnitIds)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	for (unsigned int deadId : deadUnitIds)
	{
		auto it = std::find(task->assignedUnitIds.begin(), task->assignedUnitIds.end(), deadId);
		if (it != task->assignedUnitIds.end())
		{
			task->assignedUnitIds.erase(it);
		}
	}

	return true;
}

bool AIControlAdapterCombatTaskManager::releaseUnits(unsigned int taskId, const std::string& reason)
{
	CombatTask* task = findTask(taskId);
	if (task == nullptr)
	{
		return false;
	}

	task->assignedUnitIds.clear();
	task->reason = reason;
	return true;
}

void AIControlAdapterCombatTaskManager::updateTasks(DWORD currentTick)
{
	// Update last seen tick for active tasks
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			// Check for timeout
			if (currentTick >= task.timeoutTick)
			{
				task.state = CombatTaskState::Expired;
				task.reason = "timeout";
			}
		}
	}
}

void AIControlAdapterCombatTaskManager::pruneExpiredTasks(DWORD currentTick)
{
	std::vector<unsigned int> toRemove;

	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;

		// Remove terminal tasks after a grace period
		if (task.state == CombatTaskState::Complete ||
			task.state == CombatTaskState::Failed ||
			task.state == CombatTaskState::Expired)
		{
			// Keep terminal tasks for 5 seconds for telemetry
			const DWORD terminalAge = currentTick - task.lastCommandTick;
			if (terminalAge > 5000)
			{
				toRemove.push_back(task.taskId);
			}
		}
	}

	for (unsigned int taskId : toRemove)
	{
		tasks.erase(taskId);
	}
}

void AIControlAdapterCombatTaskManager::pruneDeadUnits(DWORD currentTick)
{
	// Dead unit pruning should be done by the caller using removeDeadUnits
	// This is a placeholder for future automatic dead unit detection
}

void AIControlAdapterCombatTaskManager::removeTask(unsigned int taskId)
{
	tasks.erase(taskId);
}

std::vector<CombatTask*> AIControlAdapterCombatTaskManager::findAttackTasks()
{
	std::vector<CombatTask*> result;
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Attack &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

std::vector<CombatTask*> AIControlAdapterCombatTaskManager::findDefenseTasks()
{
	std::vector<CombatTask*> result;
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Defense &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

std::vector<CombatTask*> AIControlAdapterCombatTaskManager::findActiveTasks()
{
	std::vector<CombatTask*> result;
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			result.push_back(&task);
		}
	}
	return result;
}

CombatTask* AIControlAdapterCombatTaskManager::findActiveTaskByType(
	CombatTaskType type,
	const std::string& owner)
{
	for (auto& pair : tasks)
	{
		CombatTask& task = pair.second;
		if (task.type == type &&
			task.owner == owner &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			return &task;
		}
	}
	return nullptr;
}

bool AIControlAdapterCombatTaskManager::hasEquivalentActiveTask(
	CombatTaskType type,
	const Coord3D& targetPosition,
	Real toleranceRadius) const
{
	const Real toleranceRadiusSq = toleranceRadius * toleranceRadius;

	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == type &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			const Real dx = task.targetPosition.x - targetPosition.x;
			const Real dy = task.targetPosition.y - targetPosition.y;
			const Real distSq = dx * dx + dy * dy;
			if (distSq <= toleranceRadiusSq)
			{
				return true;
			}
		}
	}
	return false;
}

int AIControlAdapterCombatTaskManager::getActiveTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getAttackTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Attack &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getDefenseTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Defense &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getGuardTaskCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.type == CombatTaskType::Guard &&
			task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			++count;
		}
	}
	return count;
}

int AIControlAdapterCombatTaskManager::getTotalAssignedUnitCount() const
{
	int count = 0;
	for (const auto& pair : tasks)
	{
		const CombatTask& task = pair.second;
		if (task.state != CombatTaskState::Complete &&
			task.state != CombatTaskState::Failed &&
			task.state != CombatTaskState::Expired)
		{
			count += static_cast<int>(task.assignedUnitIds.size());
		}
	}
	return count;
}
