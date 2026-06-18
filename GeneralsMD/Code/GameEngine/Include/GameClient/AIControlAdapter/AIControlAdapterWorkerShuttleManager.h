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

class AIControlAdapterWorkerShuttleManager
{
public:
	int ResolveProtectedTechnicalCount(const nlohmann::json& glaUsaStrategyTelemetry) const;

	std::set<unsigned int> CollectProtectedTechnicalIds(
		const std::vector<AIControlAdapterWorkerShuttleTechnicalSnapshot>& technicals,
		int desiredProtected) const;

	bool IsTechnicalProtected(
		const AIControlAdapterWorkerShuttleTechnicalSnapshot& technical,
		const std::set<unsigned int>& protectedIds) const;

	bool IsTechnicalAssigned(
		unsigned int technicalId,
		const std::vector<AIControlAdapterWorkerShuttleAssignmentSnapshot>& assignments) const;

	nlohmann::json BuildAssignmentTelemetry(
		const AIControlAdapterWorkerShuttleAssignmentSnapshot& assignment,
		const AIControlAdapterWorkerShuttleAssignmentStatus& status,
		unsigned int now) const;
};
