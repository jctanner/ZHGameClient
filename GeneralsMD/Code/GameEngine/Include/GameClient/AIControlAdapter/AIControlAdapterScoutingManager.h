#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"
#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>
#include <vector>

struct AIControlAdapterScoutMapBounds
{
	float minX = 0.0f;
	float minY = 0.0f;
	float maxX = 5000.0f;
	float maxY = 5000.0f;
};

struct AIControlAdapterScoutLastZoneSnapshot
{
	bool hasLastZone = false;
	unsigned int anchorId = 0u;
	bool isMainBase = false;
	float centerX = 0.0f;
	float centerY = 0.0f;
};

class AIControlAdapterScoutingManager
{
public:
	static unsigned int buildStableScoutSeed(
		const std::string& mapName,
		int playerIndex,
		unsigned int currentTick,
		int scoutTaskCount);

	static AIControlAdapterScoutMapBounds resolveScoutMapBounds(
		const nlohmann::json& pathingTelemetry,
		const nlohmann::json& telemetryZones);

	static std::vector<CombatTaskScoutRandomOrigin> buildScoutRandomOrigins(
		const nlohmann::json& telemetryZones,
		const AIControlAdapterScoutLastZoneSnapshot& lastZone);

	static std::vector<CombatTaskScoutRandomBarrier> buildScoutRandomBarriers(
		const nlohmann::json& pathingTelemetry);
};
