#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterEnemyMemory.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"
#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>
#include <vector>

struct AIControlAdapterScudStormMemorySnapshot
{
	unsigned int objectId = 0u;
	int playerIndex = -1;
	int team = -1;
	std::string targetKind;
	std::string templateName;
	bool visible = false;
	bool stale = false;
	bool enemyOwned = true;
	bool alive = true;
	unsigned int lastSeenTick = 0u;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct AIControlAdapterScudStormZoneThreatSnapshot
{
	unsigned int zoneId = 0u;
	unsigned int lastSeenTick = 0u;
};

struct AIControlAdapterScudStormPlacementChoice
{
	bool hasPlacement = false;
	unsigned int zoneId = 0u;
	std::string role = "unavailable";
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float radius = 320.0f;
	int score = -999999;
	std::string reason = "unavailable";
};

class AIControlAdapterScudStormManager
{
public:
	static int countSupplyZones(const nlohmann::json& telemetryZones);

	static std::vector<AIControlAdapterScudStormStrategicTargetCandidate> buildStrategicTargetCandidates(
		const std::vector<AIControlAdapterScudStormMemorySnapshot>& memoryItems,
		unsigned int now);

	static std::vector<AIControlAdapterScudStormMemorySnapshot> buildMemorySnapshotsFromEnemyMemory(
		const std::vector<EnemyMemoryItem>& memoryItems);

	static AIControlAdapterScudStormPlacementChoice choosePlacement(
		const nlohmann::json& telemetryZones,
		const std::vector<AIControlAdapterScudStormZoneThreatSnapshot>& zoneThreats,
		unsigned int now,
		float defaultZoneRadius);
};
