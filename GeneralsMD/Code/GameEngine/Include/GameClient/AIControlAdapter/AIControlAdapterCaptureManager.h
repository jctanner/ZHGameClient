#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterTaskReservation.h"

#include <vector>

struct AIControlAdapterCaptureProgressResult
{
	bool measured;
	bool nearTarget;
	bool loggedProgress;
	const char* progressReason;
	bool updateState;
	SpecialTaskState newState;
	const char* stateReason;
	const char* stateLogName;

	AIControlAdapterCaptureProgressResult();
};

struct AIControlAdapterCaptureCommandDecision
{
	bool shouldReissue;
	const char* reason;
	unsigned int staleDurationMs;
	unsigned int timeSinceLastCommandMs;

	AIControlAdapterCaptureCommandDecision();
};

struct AIControlAdapterCaptureExpirationDecision
{
	bool shouldExpire;
	const char* reason;
	unsigned int staleDurationMs;

	AIControlAdapterCaptureExpirationDecision();
};

struct AIControlAdapterCaptureAssignmentBudgetDecision
{
	bool canAssign;
	int assignmentsRemaining;
	int attemptsRemaining;
	const char* reason;

	AIControlAdapterCaptureAssignmentBudgetDecision();
};

class AIControlAdapterCaptureManager
{
public:
	static bool IsTerminalState(SpecialTaskState state);

	static const SpecialTaskReservation* FindActiveCaptureReservationForObject(
		const std::vector<SpecialTaskReservation*>& captureTasks,
		unsigned int objectId);

	static bool IsCaptureTargetReserved(
		const std::vector<SpecialTaskReservation*>& captureTasks,
		unsigned int targetObjectId);

	static bool IsCaptureSourceReserved(
		bool taskReserved,
		bool garrisonReserved);

	static AIControlAdapterCaptureProgressResult ApplyProgressMeasurement(
		SpecialTaskReservation& task,
		unsigned int now,
		Real currentDistance);

	static AIControlAdapterCaptureCommandDecision ChooseCommandReissue(
		const SpecialTaskReservation& task,
		unsigned int now,
		Real currentDistance,
		bool nearTarget);

	static AIControlAdapterCaptureExpirationDecision ChooseExpiration(
		const SpecialTaskReservation& task,
		unsigned int now,
		Real currentDistance,
		bool nearTarget);

	static AIControlAdapterCaptureAssignmentBudgetDecision ChooseAssignmentBudget(
		int pendingCount,
		int maxConcurrent,
		int attemptsPerAssignment = 4);

	static void ApplyCommandReissueSuccess(
		SpecialTaskReservation& task,
		unsigned int now,
		unsigned int timeoutExtensionMs = 60000u);
};
