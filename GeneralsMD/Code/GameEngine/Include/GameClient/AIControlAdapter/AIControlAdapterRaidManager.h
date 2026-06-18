#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterCombatTask.h"

#include <string>
#include <vector>

struct AIControlAdapterRaidProbeUnitSnapshot
{
	unsigned int unitId = 0u;
	std::string templateName;
	bool alive = true;
	bool vehicle = false;
	bool aircraft = false;
	bool ableToAttack = true;
	bool underConstruction = false;
	bool worker = false;
	bool harvester = false;
	bool taskReserved = false;
	bool zoneDefenseFloorReserved = false;
	float x = 0.0f;
	float y = 0.0f;
	bool hasPosition = false;
};

struct AIControlAdapterRaidModeDecision
{
	std::string raidMode;
	bool degradeToVehicle = false;
	bool fail = false;
	const char* reason = "ok";
};

class AIControlAdapterRaidManager
{
public:
	static bool containsIgnoreCase(const std::string& haystack, const std::string& needle);

	static std::vector<unsigned int> selectProbeUnitIds(
		const std::vector<AIControlAdapterRaidProbeUnitSnapshot>& units,
		const CombatTaskWaypoint& waypoint,
		int maxProbeUnits = 3,
		float maxDistanceFromAnchor = 1200.0f);

	static Coord3D computeProbeTarget(
		const std::vector<CombatTaskWaypoint>& waypoints,
		const Coord3D& finalTarget,
		int waypointIndex,
		float maxProbeAdvance = 1200.0f);

	static AIControlAdapterRaidModeDecision evaluateRaidModePolicy(
		const std::string& currentRaidMode,
		int infantryLive,
		int vehicleLive,
		int initialUnitCount,
		int minimumViableCount,
		bool cohesionWaitStarted,
		unsigned int cohesionWaitMs,
		unsigned int mixedInfantryTimeoutMs = 45000u);
};
