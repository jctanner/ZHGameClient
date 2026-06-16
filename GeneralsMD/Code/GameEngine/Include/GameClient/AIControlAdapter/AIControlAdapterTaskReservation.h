#pragma once

#include <string>
#include <vector>
#include <map>

// Forward declarations
struct Coord3D;

//==============================================================================
// Phase 6.1: Special Operations Task Reservations
//
// Provides task lifecycle tracking for individual units executing fragile,
// long-running operations such as construction or capture.
//
// Construction task ownership cannot end when a foundation appears - the task
// must remain alive until the structure is complete, failed, expired, or
// explicitly recovered to another worker.
//==============================================================================

enum class SpecialTaskType
{
	BuildStructure,
	CaptureStructure,
	Repair,
	SpecialAbility
};

enum class SpecialTaskState
{
	Assigned,    // Worker/source assigned, command issued
	Moving,      // Unit moving toward target (optional state)
	Executing,   // Foundation found or capture in progress
	Verifying,   // Checking completion (optional state)
	Complete,    // Task successfully finished
	Failed,      // Task failed (source dead, target invalid, etc.)
	Expired      // Task timed out
};

struct SpecialTaskReservation
{
	unsigned int taskId = 0;
	SpecialTaskType type = SpecialTaskType::BuildStructure;
	SpecialTaskState state = SpecialTaskState::Assigned;

	unsigned int sourceObjectId = 0;      // worker, rebel, hijacker, etc.
	unsigned int targetObjectId = 0;      // capturable building, repair target, optional
	std::string expectedTemplate;         // expected building template for build tasks
	Coord3D targetPosition;
	std::string owner;                    // macro, economy, capture, static_objective

	DWORD createdTick = 0;
	DWORD lastSeenTick = 0;
	DWORD lastUpdateTick = 0;
	DWORD timeoutTick = 0;

	// Phase 6.2: Progress tracking for capture tasks
	Real lastDistance = -1.0f;            // Last measured distance to target (-1 = not measured)
	Real bestDistance = -1.0f;            // Best (minimum) distance achieved (-1 = not measured)
	DWORD lastCommandTick = 0;            // Last time command was issued/reissued
	int commandReissueCount = 0;          // Number of times command has been reissued

	std::string reason;                   // State transition or failure reason
};

struct StoppedFoundationTombstone
{
	unsigned int foundationObjectId = 0;
	std::string expectedTemplate;
	Coord3D position;
	std::string reason;
	DWORD createdTick = 0;
	DWORD expiresTick = 0;
	DWORD lastSkipLogTick = 0;
};

class AIControlAdapterTaskReservationManager
{
public:
	AIControlAdapterTaskReservationManager();

	// Create a new task reservation
	unsigned int createReservation(
		SpecialTaskType type,
		unsigned int sourceObjectId,
		const std::string& expectedTemplate,
		const Coord3D& targetPosition,
		const std::string& owner,
		unsigned int timeoutMs = 90000); // 90 seconds default

	// Create capture task reservation
	unsigned int createCaptureReservation(
		unsigned int sourceObjectId,
		unsigned int targetObjectId,
		const std::string& owner,
		unsigned int timeoutMs = 60000); // 60 seconds default

	// Query reservations
	bool isObjectReserved(unsigned int objectId) const;
	bool canUseObjectForTask(unsigned int objectId, const std::string& requestingOwner) const;
	SpecialTaskReservation* findReservation(unsigned int taskId);
	const SpecialTaskReservation* findReservation(unsigned int taskId) const;
	SpecialTaskReservation* findReservationBySource(unsigned int sourceObjectId);
	const SpecialTaskReservation* findReservationBySource(unsigned int sourceObjectId) const;

	// Update task state
	bool updateTaskState(
		unsigned int taskId,
		SpecialTaskState newState,
		const std::string& reason);

	// Set target object ID (for when foundation is found)
	bool setTaskTargetObject(unsigned int taskId, unsigned int targetObjectId);

	// Mark task complete
	bool completeTask(unsigned int taskId, const std::string& reason);

	// Mark task failed
	bool failTask(unsigned int taskId, const std::string& reason);

	// Mark task expired
	bool expireTask(unsigned int taskId, const std::string& reason);

	// Lifecycle management
	void updateTasks(DWORD currentTick);
	void pruneExpiredTasks(DWORD currentTick);
	void removeTask(unsigned int taskId);

	// Stopped stale foundations should not be re-adopted as orphan build tasks.
	void tombstoneStoppedFoundation(
		unsigned int foundationObjectId,
		const std::string& expectedTemplate,
		const Coord3D& position,
		const std::string& reason,
		DWORD currentTick,
		unsigned int ttlMs = 120000);
	bool isFoundationTombstoned(
		unsigned int foundationObjectId,
		DWORD currentTick,
		DWORD* ageMs = nullptr,
		std::string* reason = nullptr) const;
	bool shouldLogFoundationTombstoneSkip(
		unsigned int foundationObjectId,
		DWORD currentTick,
		unsigned int throttleMs = 5000);
	void clearFoundationTombstone(unsigned int foundationObjectId);

	// Queries for specific task types
	std::vector<SpecialTaskReservation*> findBuildTasks(const std::string& templateFilter = "");
	std::vector<SpecialTaskReservation*> findCaptureTasks();
	std::vector<SpecialTaskReservation*> findActiveTasks();

	// Statistics
	int getActiveTaskCount() const;
	int getBuildTaskCount() const;
	int getCaptureTaskCount() const;

private:
	unsigned int nextTaskId = 1;
	std::map<unsigned int, SpecialTaskReservation> reservations;
	std::map<unsigned int, StoppedFoundationTombstone> stoppedFoundationTombstones;
};
