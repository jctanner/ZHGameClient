#pragma once

#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>
#include <vector>

struct AIControlAdapterGarrisonDiscoveryFacts
{
	bool palace = false;
	bool kindFlag = false;
	bool containGarrison = false;
	bool uiEnterable = false;
	bool mapCacheGarrison = false;
	bool hasContain = false;
	int capacity = 0;
	std::string templateName;
};

struct AIControlAdapterGarrisonDiscoveryResult
{
	bool accepted = false;
	bool palace = false;
	bool kindFlag = false;
	bool containGarrison = false;
	bool uiEnterable = false;
	bool mapCacheGarrison = false;
	int capacity = 0;
	std::string containName = "none";
	std::string reason = "not_garrisonable";
};

struct AIControlAdapterGarrisonInfantryTelemetry
{
	unsigned int unitId = 0u;
	std::string templateName;
	float lastX = 0.0f;
	float lastY = 0.0f;
	float lastDistance = -1.0f;
	bool entered = false;
	bool enteredPendingVerification = false;
	bool outside = false;
	bool nearby = false;
	unsigned int lastProgressTick = 0u;
	unsigned int lastCommandTick = 0u;
	std::string state = "assigned";
	std::string reason = "assigned";
};

struct AIControlAdapterGarrisonAssignmentTelemetry
{
	unsigned int structureId = 0u;
	unsigned int zoneAnchorId = 0u;
	std::string templateName;
	float x = 0.0f;
	float y = 0.0f;
	int desiredInfantry = 0;
	int estimatedCapacity = 0;
	unsigned int lastProgressTick = 0u;
	unsigned int lastCommandTick = 0u;
	std::string state;
	std::string reason;
	std::vector<unsigned int> infantryIds;
	std::vector<AIControlAdapterGarrisonInfantryTelemetry> infantry;
};

class AIControlAdapterGarrisonManager
{
public:
	static bool containsIgnoreCase(const std::string& haystack, const std::string& needle);

	static bool isNearMapCacheGarrison(
		const nlohmann::json& pathingTelemetry,
		const std::string& templateName,
		float x,
		float y,
		float radius);

	static AIControlAdapterGarrisonDiscoveryResult evaluateDiscovery(
		const AIControlAdapterGarrisonDiscoveryFacts& facts);

	static bool shouldLogDiscovery(
		const std::string& templateName,
		const AIControlAdapterGarrisonDiscoveryResult& discovery);

	static bool isNearGarrisonFeature(
		const nlohmann::json& pathingTelemetry,
		float x,
		float y,
		const char* wantedKind,
		float radius);

	static int countEnteredInfantry(const AIControlAdapterGarrisonAssignmentTelemetry& assignment);

	static nlohmann::json buildAssignmentTelemetry(
		const AIControlAdapterGarrisonAssignmentTelemetry& assignment,
		unsigned int now);
};
