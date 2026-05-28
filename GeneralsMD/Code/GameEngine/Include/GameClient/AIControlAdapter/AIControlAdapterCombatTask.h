#pragma once

#include <string>
#include <vector>
#include <map>

// Forward declarations
struct Coord3D;

//==============================================================================
// Phase 9: Combat Task Ownership
//
// Provides durable task tracking for combat unit groups executing offensive,
// defensive, and guard operations. Combat tasks reserve assigned units to
// prevent them from being stolen by unrelated automation systems.
//
// Unlike Phase 6.1 special operations (individual workers/rebels), combat tasks
// track groups of units with collective objectives.
//==============================================================================

enum class CombatTaskType
{
	Attack,     // Offensive raid/attack
	Defense,    // Defensive response to zone threats
	Guard       // Positional guard (optional, may be added later)
};

enum class CombatTaskState
{
	Assembling,  // Gathering units for the task
	Moving,      // Moving toward target position
	Engaging,    // Engaged with enemy or at target
	Complete,    // Task successfully finished
	Failed,      // Task failed (too many casualties, target invalid)
	Expired      // Task timed out
};

struct CombatTask
{
	unsigned int taskId = 0;
	CombatTaskType type = CombatTaskType::Attack;
	CombatTaskState state = CombatTaskState::Assembling;

	std::vector<unsigned int> assignedUnitIds;  // Units reserved for this task
	Coord3D targetPosition;
	unsigned int targetObjectId = 0;            // Optional target object

	std::string owner;                          // autonomous_attack, zone_defense, etc.
	std::string reason;                         // State transition or assignment reason

	DWORD createdTick = 0;
	DWORD lastCommandTick = 0;                  // Last time a command was issued
	DWORD lastProgressTick = 0;                 // Last time progress was observed
	DWORD timeoutTick = 0;

	int initialUnitCount = 0;                   // For tracking casualties
	int minimumViableCount = 0;                 // Fail if below this many units
};

class AIControlAdapterCombatTaskManager
{
public:
	AIControlAdapterCombatTaskManager();

	// Create a new combat task
	unsigned int createTask(
		CombatTaskType type,
		const std::vector<unsigned int>& unitIds,
		const Coord3D& targetPosition,
		const std::string& owner,
		const std::string& reason,
		unsigned int timeoutMs = 120000); // 2 minutes default

	// Query reservations
	bool isUnitReserved(unsigned int unitId) const;
	bool canUseUnitForTask(unsigned int unitId, const std::string& requestingOwner) const;
	CombatTask* findTask(unsigned int taskId);
	const CombatTask* findTask(unsigned int taskId) const;
	CombatTask* findTaskByUnit(unsigned int unitId);
	const CombatTask* findTaskByUnit(unsigned int unitId) const;

	// Update task state
	bool updateTaskState(
		unsigned int taskId,
		CombatTaskState newState,
		const std::string& reason);

	// Update task command tracking
	bool updateTaskCommand(
		unsigned int taskId,
		DWORD currentTick);

	// Update task progress tracking
	bool updateTaskProgress(
		unsigned int taskId,
		DWORD currentTick);

	// Mark task complete
	bool completeTask(unsigned int taskId, const std::string& reason);

	// Mark task failed
	bool failTask(unsigned int taskId, const std::string& reason);

	// Mark task expired
	bool expireTask(unsigned int taskId, const std::string& reason);

	// Unit management
	bool removeDeadUnits(unsigned int taskId, const std::vector<unsigned int>& deadUnitIds);
	bool releaseUnits(unsigned int taskId, const std::string& reason);

	// Lifecycle management
	void updateTasks(DWORD currentTick);
	void pruneExpiredTasks(DWORD currentTick);
	void pruneDeadUnits(DWORD currentTick);
	void removeTask(unsigned int taskId);

	// Queries for specific task types
	std::vector<CombatTask*> findAttackTasks();
	std::vector<CombatTask*> findDefenseTasks();
	std::vector<CombatTask*> findActiveTasks();
	CombatTask* findActiveTaskByType(CombatTaskType type, const std::string& owner);

	// Check for duplicate tasks
	bool hasEquivalentActiveTask(
		CombatTaskType type,
		const Coord3D& targetPosition,
		Real toleranceRadius = 500.0f) const;

	// Statistics
	int getActiveTaskCount() const;
	int getAttackTaskCount() const;
	int getDefenseTaskCount() const;
	int getGuardTaskCount() const;
	int getTotalAssignedUnitCount() const;

private:
	unsigned int nextTaskId = 1;
	std::map<unsigned int, CombatTask> tasks;
};
