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
	Guard,      // Positional guard (optional, may be added later)
	Scout       // Dedicated fast map reveal / enemy-memory refresh
};

enum class CombatTaskState
{
	Assembling,  // Gathering units for the task
	Forming,     // Moving to the first raid stage
	Moving,      // Moving toward target position
	MovingToStage,        // Moving toward the current staged waypoint
	WaitingForCohesion,   // Holding until enough assigned units arrive
	Advancing,            // Advancing to the next stage
	Engaging,    // Engaged with enemy or at target
	Attacking,   // Final attack-move issued
	Reassessing, // Task needs release/reinforcement/retargeting
	Complete,    // Task successfully finished
	Failed,      // Task failed (too many casualties, target invalid)
	Expired      // Task timed out
};

enum class CombatTaskProbeState
{
	Inactive,
	Moving,
	Scouting,
	Complete,
	Timeout,
	Cancelled
};

struct CombatTaskWaypoint
{
	Coord3D position;
	Real radius = 300.0f;
};

struct CombatTaskCohesionInput
{
	int liveAssigned = 0;
	int arrived = 0;
	int infantryLive = 0;
	int infantryArrived = 0;
	int minimumViable = 1;
	bool timeout = false;
	bool requireInfantryQuorum = true;
};

struct CombatTaskCohesionDecision
{
	bool shouldAdvance = false;
	bool shouldFail = false;
	int requiredQuorum = 0;
	int infantryRequiredQuorum = 0;
	const char* reason = "waiting_for_cohesion";
};

struct CombatTaskProbePolicyInput
{
	bool holdingForCohesion = false;
	bool quorumReached = false;
	unsigned int cohesionWaitMs = 0;
	unsigned int minWaitMs = 30000;
	int freshStrategicTargets = 0;
	CombatTaskProbeState probeState = CombatTaskProbeState::Inactive;
	unsigned int probeStartedTick = 0;
	unsigned int probeTimeoutMs = 20000;
	unsigned int lastProbeEndTick = 0;
	unsigned int currentTick = 0;
	unsigned int cooldownMs = 15000;
	int eligibleProbeUnits = 0;
};

struct CombatTaskProbeDecision
{
	bool shouldLaunch = false;
	bool shouldTimeout = false;
	bool shouldCancel = false;
	const char* mode = "hold";
	const char* reason = "waiting_for_cohesion";
};

struct CombatTaskProbeCandidate
{
	unsigned int unitId = 0;
	bool alive = true;
	bool fast = false;
	bool combatCapable = true;
	bool worker = false;
	bool captureTaskReserved = false;
	bool constructionTaskReserved = false;
	bool zoneDefenseFloorReserved = false;
	bool criticalBaseDefenseReserved = false;
	Real distanceFromAnchor = 0.0f;
};

struct CombatTaskScoutPolicyInput
{
	int readyScudStorms = 0;
	int freshStrategicTargets = 0;
	int staleStrategicTargets = 0;
	int objectiveCount = 0;
	int availableScouts = 0;
	int activeScoutTasks = 0;
	int maxActiveScoutTasks = 1;
	bool combatPressureHigh = false;
	bool scudTargetRefreshNeeded = false;
	int likelyRegionsRemaining = 0;
	unsigned int lastScoutRevealAgeMs = 0;
};

struct CombatTaskScoutPolicyDecision
{
	bool shouldLaunch = false;
	const char* mode = "hold";
	const char* reason = "fresh_targets_available";
};

struct CombatTaskScoutObjective
{
	unsigned int objectiveId = 0;
	Coord3D position;
	int priority = 0;
	bool staleStructure = false;
	bool likelyBase = false;
	bool cluster = false;
	bool randomReveal = false;
	unsigned int originZoneId = 0;
	float directionX = 0.0f;
	float directionY = 0.0f;
	const char* reason = "unknown";
};

struct CombatTaskScoutRandomOrigin
{
	unsigned int zoneId = 0;
	Coord3D position;
	bool mainBase = false;
	int priority = 0;
};

struct CombatTaskScoutRandomBarrier
{
	float ax = 0.0f;
	float ay = 0.0f;
	float bx = 0.0f;
	float by = 0.0f;
};

struct CombatTaskScoutRandomInput
{
	std::vector<CombatTaskScoutRandomOrigin> origins;
	std::vector<CombatTaskScoutRandomBarrier> barriers;
	unsigned int seed = 0;
	unsigned int previousObjectiveId = 0;
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = 0.0f;
	float maxY = 0.0f;
	float revealDistance = 1600.0f;
	bool coverageThin = true;
};

struct CombatTaskScoutRandomDecision
{
	bool selected = false;
	CombatTaskScoutObjective objective;
	int candidateCount = 0;
	int rejectedCount = 0;
	const char* reason = "no_random_origin";
};

struct CombatTaskScoutCandidate
{
	unsigned int unitId = 0;
	bool alive = true;
	bool fast = false;
	bool combatCapable = true;
	bool worker = false;
	bool captureTaskReserved = false;
	bool constructionTaskReserved = false;
	bool garrisonReserved = false;
	bool zoneDefenseFloorReserved = false;
	bool combatTaskReserved = false;
	bool artilleryCounterReserved = false;
	int preference = 0;
	Real distanceFromOrigin = 0.0f;
};

struct CombatTaskScudTargetRefreshOverrideInput
{
	bool scudTargetRefreshNeeded = false;
	bool noAvailableScouts = false;
	bool defenseFloorBlocked = false;
	bool mainBaseCritical = false;
	bool criticalZoneDefense = false;
	bool armsDealerReady = false;
	unsigned int money = 0;
	unsigned int reserveCash = 0;
	int activeScoutTasks = 0;
	int maxActiveScoutTasks = 1;
	int scoutPoolLive = 0;
	int scoutPoolQueued = 0;
	int scoutPoolAssigned = 0;
};

struct CombatTaskScudTargetRefreshOverrideDecision
{
	bool allowProduction = false;
	bool allowBorrow = false;
	bool relaxDefenseFloor = false;
	const char* mode = "hold";
	const char* reason = "not_needed";
};

struct CombatTaskScoutPoolInput
{
	bool armsDealerReady = false;
	bool scudTargetStarved = false;
	bool freshScoutCoverage = false;
	unsigned int money = 0;
	unsigned int reserveCash = 0;
	int liveTechnicals = 0;
	int queuedTechnicals = 0;
	int assignedScoutUnits = 0;
};

struct CombatTaskScoutPoolDecision
{
	int desiredTechnicals = 0;
	bool productionNeeded = false;
	const char* unitTemplate = "GLAVehicleTechnical";
	const char* reason = "coverage_fresh";
};

struct CombatTask
{
	unsigned int taskId = 0;
	CombatTaskType type = CombatTaskType::Attack;
	CombatTaskState state = CombatTaskState::Assembling;

	std::vector<unsigned int> assignedUnitIds;  // Units reserved for this task
	Coord3D targetPosition;
	Coord3D originPosition;
	bool hasOriginPosition = false;
	unsigned int targetObjectId = 0;            // Optional target object

	std::string owner;                          // autonomous_attack, zone_defense, etc.
	std::string reason;                         // State transition or assignment reason

	DWORD createdTick = 0;
	DWORD lastCommandTick = 0;                  // Last time a command was issued
	DWORD lastProgressTick = 0;                 // Last time progress was observed
	DWORD timeoutTick = 0;

	int initialUnitCount = 0;                   // For tracking casualties
	int minimumViableCount = 0;                 // Fail if below this many units

	std::vector<CombatTaskWaypoint> waypoints;
	int currentWaypointIndex = 0;
	DWORD currentWaypointStartTick = 0;
	DWORD stageTimeoutMs = 45000;
	int arrivedCount = 0;
	int missingCount = 0;
	int confirmedDeadCount = 0;
	int requiredQuorumCount = 0;
	int infantryCount = 0;
	int infantryArrivedCount = 0;
	int infantryRequiredQuorumCount = 0;
	int vehicleCount = 0;
	Real groupSpread = 0.0f;
	std::string cohesionReason;

	DWORD lastCohesionLogTick = 0;
	bool cohesionLogSnapshotValid = false;
	CombatTaskState lastLoggedState = CombatTaskState::Assembling;
	std::string lastLoggedReason;
	int lastLoggedWaypointIndex = -1;
	int lastLoggedArrivedCount = -1;
	int lastLoggedMissingCount = -1;
	int lastLoggedDeadCount = -1;

	DWORD lastScoutStateLogTick = 0;
	bool scoutStateLogSnapshotValid = false;
	std::string lastScoutLoggedState;
	std::string lastScoutLoggedReason;
	int lastScoutLoggedWaypointIndex = -1;
	int lastScoutLoggedFreshTargets = -1;

	CombatTaskProbeState probeState = CombatTaskProbeState::Inactive;
	std::vector<unsigned int> probeUnitIds;
	DWORD probeStartedTick = 0;
	DWORD lastProbeEndTick = 0;
	DWORD lastProbeCommandTick = 0;
	std::string probeReason;
	int freshStrategicTargets = 0;
	DWORD cohesionWaitStartTick = 0;
	DWORD scoutCompletedTick = 0;
	std::string raidMode;
	std::string quorumType;
	std::string degradedFrom;
	std::string degradeReason;
};

const char* combatTaskTypeName(CombatTaskType type);
const char* combatTaskStateName(CombatTaskState state);
const char* combatTaskProbeStateName(CombatTaskProbeState state);
std::vector<CombatTaskWaypoint> buildDirectRaidWaypoints(
	const Coord3D& origin,
	const Coord3D& target,
	Real stageRadius = 300.0f);
CombatTaskCohesionDecision evaluateCombatTaskCohesion(
	const CombatTaskCohesionInput& input);
struct CombatTaskRaidMixDecision
{
	const char* mode = "hold";
	bool allowVehicles = false;
	bool allowInfantry = false;
	bool shouldLaunch = false;
	const char* reason = "no_viable_units";
};
CombatTaskRaidMixDecision evaluateCombatTaskRaidMixPolicy(
	int vehicleCount,
	int infantryCount,
	Real targetDistance,
	Real localDistance = 1400.0f);
bool shouldLogCombatTaskCohesion(
	CombatTask& task,
	DWORD currentTick,
	DWORD heartbeatMs = 4000,
	int countDeltaThreshold = 3);
bool shouldLogCombatTaskScoutState(
	CombatTask& task,
	DWORD currentTick,
	const std::string& stateName,
	const std::string& reason,
	int waypointIndex,
	int freshTargets,
	DWORD heartbeatMs = 4000);
CombatTaskProbeDecision evaluateCombatTaskProbePolicy(
	const CombatTaskProbePolicyInput& input);
std::vector<unsigned int> selectCombatTaskProbeUnits(
	const std::vector<CombatTaskProbeCandidate>& candidates,
	int maxProbeUnits = 3,
	Real maxDistanceFromAnchor = 1200.0f);
CombatTaskScoutPolicyDecision evaluateCombatTaskScoutPolicy(
	const CombatTaskScoutPolicyInput& input);
CombatTaskScoutObjective selectCombatTaskScoutObjective(
	const std::vector<CombatTaskScoutObjective>& objectives);
CombatTaskScoutRandomDecision selectCombatTaskRandomRevealObjective(
	const CombatTaskScoutRandomInput& input);
std::vector<unsigned int> selectCombatTaskScoutUnits(
	const std::vector<CombatTaskScoutCandidate>& candidates,
	int maxScoutUnits = 2,
	bool relaxDefenseFloor = false);
CombatTaskScudTargetRefreshOverrideDecision evaluateCombatTaskScudTargetRefreshOverride(
	const CombatTaskScudTargetRefreshOverrideInput& input);
CombatTaskScoutPoolDecision evaluateCombatTaskScoutPool(
	const CombatTaskScoutPoolInput& input);

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
	int getScoutTaskCount() const;
	int getScoutAssignedUnitCount() const;
	int getTotalAssignedUnitCount() const;

private:
	unsigned int nextTaskId = 1;
	std::map<unsigned int, CombatTask> tasks;
};
