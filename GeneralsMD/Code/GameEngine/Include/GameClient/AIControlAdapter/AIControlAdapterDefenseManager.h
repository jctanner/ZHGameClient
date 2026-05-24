/**
 * AIControlAdapterDefenseManager.h
 *
 * Defense request management for AI Control Adapter autonomous behavior.
 *
 * This module determines defensive responses to zone threats:
 * - Selects highest-priority threatened zone
 * - Decides whether zone needs defenders
 * - Requests local production for threatened zones
 * - Requests fallback production from nearby zones when local producers unavailable
 *
 * Design principles:
 * 1. Ownership: DefenseManager owns defense request decisions
 * 2. Separation: Read threat state from ZoneManager, emit defense requests, don't execute commands
 * 3. Observability: Defense decisions are explicit and loggable
 * 4. Testability: Defense logic can be tested without launching the game
 *
 * Phase 3 scope (architecture-improvement-plan-codex-2026-05-26.md):
 * - Select highest-priority threatened zone from zone threats
 * - Decide if threatened zone needs defenders (based on severity, freshness, local units)
 * - Request production from threatened zone's local producers when available
 * - Request production from nearest eligible producer zone as fallback
 * - Do NOT implement full unit micro, combat simulation, or pathfinding
 *
 * DefenseManager does not own:
 * - Full unit micro
 * - Attack group control
 * - Capture logic
 * - Guard command execution
 * - Economy management
 * - Macro building goals
 * - Combat simulation
 * - Full CombatTaskManager
 * - Full Scheduler/Executor
 *
 * Integration with ProductionManager:
 * - DefenseManager emits defense production requests
 * - ProductionManager consumes defense requests as production guidance
 * - Existing production behavior continues when no zone threatened
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterZoneManager.h"

#include <string>
#include <vector>

/**
 * Defense production request.
 *
 * Describes what defenders are needed for a threatened zone and where to produce them.
 */
struct DefenseRequest
{
	bool active;                      // True if defense request is active
	unsigned int threatenedZoneId;    // Anchor ID of threatened zone
	unsigned int producerZoneId;      // Anchor ID of zone to produce defenders
	bool isLocalProduction;           // True if producing in threatened zone, false if fallback
	std::string fallbackReason;       // Why fallback zone was chosen (for logging)
	std::string severityLevel;        // "low", "medium", "high"
	unsigned int threatAge;           // Ticks since zone was attacked
	int desiredSoldiers;              // Desired soldier count
	int desiredRpg;                   // Desired RPG count
	int desiredQuads;                 // Desired quad count
	int desiredScorpions;             // Desired scorpion count

	DefenseRequest()
		: active(false)
		, threatenedZoneId(0)
		, producerZoneId(0)
		, isLocalProduction(false)
		, severityLevel("low")
		, threatAge(0)
		, desiredSoldiers(0)
		, desiredRpg(0)
		, desiredQuads(0)
		, desiredScorpions(0)
	{}
};

/**
 * Defense manager inputs.
 *
 * Aggregates threat state and production availability for defense decisions.
 */
struct DefenseManagerInputs
{
	unsigned int currentTick;                          // Current game tick
	const MostThreatenedZoneResult* mostThreatenedZone; // Most threatened zone from ZoneManager
	const std::vector<ZoneSnapshot>* zones;            // Available zones with producer info
	unsigned int money;                                // Available money
	int currentSoldiers;                               // Current soldier count
	int currentRpg;                                    // Current RPG count
	int currentQuads;                                  // Current quad count
	int currentScorpions;                              // Current scorpion count
	bool defenseEnabled;                               // Whether defense is enabled (from profile/bias)

	DefenseManagerInputs()
		: currentTick(0)
		, mostThreatenedZone(nullptr)
		, zones(nullptr)
		, money(0)
		, currentSoldiers(0)
		, currentRpg(0)
		, currentQuads(0)
		, currentScorpions(0)
		, defenseEnabled(true)
	{}
};

/**
 * Defense manager for threat response decisions.
 *
 * Manages defense production requests based on zone threats and producer availability.
 * Extracted from AIControlAdapter main loop to provide clear defense decision ownership.
 */
class AIControlAdapterDefenseManager
{
public:
	AIControlAdapterDefenseManager();

	/**
	 * Choose defense production for current threatened zone.
	 *
	 * Determines what defenders are needed and where to produce them.
	 * Returns defense request that ProductionManager can use for production decisions.
	 *
	 * Decision process:
	 * 1. Check if any zone is currently threatened (from ZoneManager)
	 * 2. Decide if threat is severe enough to warrant defense response
	 * 3. Check if threatened zone has local producers
	 * 4. If local producers available, request production from threatened zone
	 * 5. If no local producers, find nearest eligible producer zone
	 * 6. Determine desired defender mix based on threat severity and current units
	 *
	 * @param inputs Current threat state and production availability
	 * @param zoneManager ZoneManager for producer zone queries
	 * @return Defense production request
	 */
	DefenseRequest ChooseDefenseProduction(
		const DefenseManagerInputs& inputs,
		const AIControlAdapterZoneManager& zoneManager);

	/**
	 * Reset defense manager state (called when starting new game).
	 */
	void Reset();

private:
	/**
	 * Decide if threat is severe enough to warrant defense response.
	 *
	 * @param threat Threatened zone result
	 * @param inputs Defense manager inputs
	 * @return True if defense needed
	 */
	bool ShouldRespondToThreat(
		const MostThreatenedZoneResult& threat,
		const DefenseManagerInputs& inputs) const;

	/**
	 * Calculate desired defender mix based on threat severity.
	 *
	 * @param severityLevel "low", "medium", "high"
	 * @param currentSoldiers Current soldier count
	 * @param currentRpg Current RPG count
	 * @param currentQuads Current quad count
	 * @param currentScorpions Current scorpion count
	 * @param outRequest Defense request to populate with desired units
	 */
	void CalculateDefenderMix(
		const std::string& severityLevel,
		int currentSoldiers,
		int currentRpg,
		int currentQuads,
		int currentScorpions,
		DefenseRequest& outRequest) const;

	/**
	 * Find local producer zone for threatened zone.
	 *
	 * @param threatenedZoneId Threatened zone anchor ID
	 * @param zones Available zones
	 * @return Zone snapshot if found, nullptr otherwise
	 */
	const ZoneSnapshot* FindZoneByAnchorId(
		unsigned int anchorId,
		const std::vector<ZoneSnapshot>& zones) const;
};
