#pragma once

#include "GameNetwork/GeneralsOnline/json.hpp"

#include <set>
#include <string>
#include <vector>

struct AIControlAdapterWorkerShuttleTechnicalSnapshot
{
	unsigned int id = 0u;
	bool isStructure = false;
	bool underConstruction = false;
	bool isTechnical = false;
	bool dead = false;
};

struct AIControlAdapterWorkerShuttleAssignmentSnapshot
{
	unsigned int taskId = 0u;
	unsigned int workerId = 0u;
	unsigned int technicalId = 0u;
	std::string templateName;
	std::string state;
	std::string reason;
	unsigned int createdTick = 0u;
	unsigned int lastCommandTick = 0u;
	float targetX = 0.0f;
	float targetY = 0.0f;
	float targetZ = 0.0f;
};

struct AIControlAdapterWorkerShuttleAssignmentStatus
{
	bool workerInside = false;
	float workerDistance = 999999.0f;
	float technicalDistance = 999999.0f;
};

struct AIControlAdapterTechnicalScoutShuttleInput
{
	int readyScudStorms = 0;
	bool scudTargetRefreshNeeded = false;
	int availableTechnicals = 0;
	int protectedTechnicals = 0;
	int remoteBuildGap = 0;
	int availableWorkers = 0;
	int availableRpg = 0;
	int availableRebels = 0;
	int activeScoutTasks = 0;
};

struct AIControlAdapterTechnicalScoutShuttleDecision
{
	bool desired = false;
	const char* mode = "pure_scout";
	const char* reason = "not_needed";
	int workerPassengers = 0;
	int rpgPassengers = 0;
	int rebelPassengers = 0;
	bool allowProtectedTechnicalScout = false;
};

class AIControlAdapterWorkerShuttleManager
{
public:
	int ResolveProtectedTechnicalCount(const nlohmann::json& glaUsaStrategyTelemetry) const;

	AIControlAdapterTechnicalScoutShuttleDecision EvaluateTechnicalScoutShuttle(
		const AIControlAdapterTechnicalScoutShuttleInput& input) const;

	std::set<unsigned int> CollectProtectedTechnicalIds(
		const std::vector<AIControlAdapterWorkerShuttleTechnicalSnapshot>& technicals,
		int desiredProtected) const;

	bool IsTechnicalProtected(
		const AIControlAdapterWorkerShuttleTechnicalSnapshot& technical,
		const std::set<unsigned int>& protectedIds) const;

	bool IsTechnicalAssigned(
		unsigned int technicalId,
		const std::vector<AIControlAdapterWorkerShuttleAssignmentSnapshot>& assignments) const;

	bool ShouldReleaseAssignmentForReservation(
		bool reservationMissing,
		bool reservationTerminal) const;

	bool IsBuildTaskEligibleForAssignment(
		const std::string& taskState,
		bool workerAlreadyContained,
		float workerDistance) const;

	nlohmann::json BuildAssignmentTelemetry(
		const AIControlAdapterWorkerShuttleAssignmentSnapshot& assignment,
		const AIControlAdapterWorkerShuttleAssignmentStatus& status,
		unsigned int now) const;
};
