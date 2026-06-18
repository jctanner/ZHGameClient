/**
 * AIControlAdapterZoneManager.h
 *
 * Zone state and threat memory management for AI Control Adapter autonomous behavior.
 *
 * This module extracts zone threat tracking from the main autonomy loop to provide:
 * - Recent attack memory per zone
 * - Threat freshness and expiration
 * - Simple threat severity classification
 * - Zone producer availability summary
 * - Nearest eligible producer zone lookup
 *
 * Design principles:
 * 1. Ownership: ZoneManager owns zone threat memory
 * 2. Separation: Read game state from inputs, emit threat state, don't execute commands
 * 3. Observability: Zone threats are explicit and loggable
 * 4. Testability: Threat logic can be tested without launching the game
 *
 * Phase 3 scope (architecture-improvement-plan-codex-2026-05-26.md):
 * - Extract recent attacked-zone memory from adapter
 * - Manage threat decay and expiration
 * - Classify threat severity
 * - Support defense manager with zone state queries
 * - Do NOT implement full combat micro, pathfinding, or unit task ownership
 *
 * Related features:
 * - Zone-based producer selection for defense (improves recovery after attacks)
 * - Explicit threat prioritization when multiple zones attacked
 * - Bounded threat memory window (prevents stale threats from affecting decisions)
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

/**
 * Zone threat memory entry.
 *
 * Records when and where a zone was attacked, how severe the attack was,
 * and when the threat should expire.
 */
struct ZoneThreatMemory
{
	unsigned int zoneAnchorId;        // Anchor object ID for this zone
	unsigned int lastAttackedTick;    // Game tick when zone was last attacked
	float lastAttackX;                // X position of attacked structure
	float lastAttackY;                // Y position of attacked structure
	float damageDelta;                // Damage amount from last attack
	std::string severityLevel;        // "low", "medium", "high"
	unsigned int damagedObjectId;     // ID of damaged structure

	ZoneThreatMemory()
		: zoneAnchorId(0)
		, lastAttackedTick(0)
		, lastAttackX(0.0f)
		, lastAttackY(0.0f)
		, damageDelta(0.0f)
		, damagedObjectId(0)
	{}
};

// Forward declare ZoneAnchorType from Policy.h
enum class ZoneAnchorType;

/**
 * Zone state snapshot for threat and producer queries.
 *
 * Minimal subset of zone information needed for threat tracking and defense decisions.
 * Phase 5.8: Extended with anchor_type to support non-supply strategic zones.
 */
struct ZoneSnapshot
{
	unsigned int anchorId;         // Anchor object ID
	float centerX;                 // Zone center X
	float centerY;                 // Zone center Y
	bool isMainBase;               // True if this is the main base zone
	int barracks;                  // Number of completed barracks in zone
	int armsDealers;               // Number of completed arms dealers in zone
	ZoneAnchorType anchorType;     // Type of anchor (main_base, supply_stash, captured_structure, etc.)

	ZoneSnapshot();
};

/**
 * Engine-independent candidate for debug overlay zone rendering.
 *
 * The adapter collects these from live objects; ZoneManager owns the
 * repeatable filtering and spacing rules.
 */
struct DebugZoneAnchorCandidate
{
	unsigned int anchorId;
	float x;
	float y;
	std::string name;
	bool isStructure;
	bool underConstruction;
	bool isSupplyStructure;

	DebugZoneAnchorCandidate();
};

/**
 * Zone anchor selected for the in-game debug overlay.
 */
struct DebugZoneAnchor
{
	unsigned int anchorId;
	float x;
	float y;
	bool isMainBase;
	ZoneAnchorType anchorType;

	DebugZoneAnchor();
};

/**
 * Structure damage event for threat detection.
 *
 * Reports friendly structure damage that may indicate a zone is under attack.
 */
struct StructureDamageEvent
{
	unsigned int objectId;         // Damaged structure ID
	float positionX;               // Damage position X
	float positionY;               // Damage position Y
	float damageDelta;             // Damage amount
	std::string severityLevel;     // "low", "medium", "high"

	StructureDamageEvent()
		: objectId(0)
		, positionX(0.0f)
		, positionY(0.0f)
		, damageDelta(0.0f)
	{}
};

/**
 * Inputs for ZoneManager threat update.
 *
 * Aggregates all state needed for threat tracking without maintaining duplicate world state.
 */
struct ZoneManagerInputs
{
	unsigned int currentTick;                                // Current game tick
	const std::vector<ZoneSnapshot>* zones;                  // Available zones
	const std::vector<StructureDamageEvent>* damageEvents;   // Recent structure damage
	float zoneRadius;                                        // Zone association radius
	unsigned int threatMemoryWindowMs;                       // How long threats remain relevant (default 45000ms)

	ZoneManagerInputs()
		: currentTick(0)
		, zones(nullptr)
		, damageEvents(nullptr)
		, zoneRadius(300.0f)
		, threatMemoryWindowMs(45000)
	{}
};

/**
 * Zone threat query result.
 *
 * Returns the most threatened zone and its threat details.
 */
struct MostThreatenedZoneResult
{
	bool hasThreat;                // True if any zone is threatened
	unsigned int zoneAnchorId;     // Threatened zone anchor ID
	unsigned int threatAge;        // Ticks since last attack
	std::string severityLevel;     // "low", "medium", "high"
	float threatenedX;             // Attack position X
	float threatenedY;             // Attack position Y

	MostThreatenedZoneResult()
		: hasThreat(false)
		, zoneAnchorId(0)
		, threatAge(0)
		, threatenedX(0.0f)
		, threatenedY(0.0f)
	{}
};

/**
 * Nearest eligible producer zone query result.
 *
 * Returns the closest zone with eligible producers for defense production.
 */
struct NearestProducerZoneResult
{
	bool found;                    // True if eligible producer zone found
	unsigned int zoneAnchorId;     // Producer zone anchor ID
	float zoneCenterX;             // Producer zone center X
	float zoneCenterY;             // Producer zone center Y
	float distance;                // Distance from threatened zone
	std::string fallbackReason;    // Why this zone was chosen (for logging)

	NearestProducerZoneResult()
		: found(false)
		, zoneAnchorId(0)
		, zoneCenterX(0.0f)
		, zoneCenterY(0.0f)
		, distance(0.0f)
	{}
};

/**
 * Zone manager for zone threat tracking and queries.
 *
 * Manages zone threat memory, threat decay, and zone state queries for defense decisions.
 * Extracted from AIControlAdapter main loop to provide clear threat memory ownership.
 */
class AIControlAdapterZoneManager
{
public:
	AIControlAdapterZoneManager();

	/**
	 * Update zone threat memory based on recent structure damage.
	 *
	 * Processes recent damage events, associates them with zones, and updates threat memory.
	 * Expires old threats that are beyond the memory window.
	 *
	 * @param inputs Current game state for threat detection
	 */
	void UpdateThreats(const ZoneManagerInputs& inputs);

	/**
	 * Get the most threatened zone.
	 *
	 * Returns the zone with the freshest/highest-severity threat.
	 * If multiple zones threatened, newest attack wins.
	 *
	 * @param currentTick Current game tick
	 * @return Most threatened zone result
	 */
	MostThreatenedZoneResult GetMostThreatenedZone(unsigned int currentTick) const;

	/**
	 * Find nearest eligible producer zone for defense production.
	 *
	 * Given a threatened zone, finds the closest zone with available producers.
	 * Used for fallback production when threatened zone has no local producers.
	 *
	 * @param threatenedZoneAnchorId Anchor ID of threatened zone
	 * @param zones Available zones
	 * @return Nearest producer zone result
	 */
	NearestProducerZoneResult FindNearestProducerZone(
		unsigned int threatenedZoneAnchorId,
		const std::vector<ZoneSnapshot>& zones) const;

	/**
	 * Check if a zone has eligible producers for defense production.
	 *
	 * @param zone Zone to check
	 * @return True if zone has barracks or arms dealers
	 */
	bool ZoneHasEligibleProducers(const ZoneSnapshot& zone) const;

	/**
	 * Build compact zone anchors for debug overlay rendering.
	 *
	 * Filters to completed eligible structures, collapses anchors that are
	 * materially overlapping, and marks the first retained anchor as main base
	 * to preserve the adapter's historical rendering behavior.
	 */
	static std::vector<DebugZoneAnchor> BuildDebugOverlayZones(
		const std::vector<DebugZoneAnchorCandidate>& candidates,
		float minimumSpacing = 200.0f);

	/**
	 * Reset zone manager state (called when starting new game).
	 */
	void Reset();

private:
	/**
	 * Zone threat memory storage.
	 * Key: zone anchor ID
	 * Value: threat memory entry
	 */
	std::unordered_map<unsigned int, ZoneThreatMemory> m_threats;

	/**
	 * Associate a damage event with the nearest zone.
	 *
	 * @param damage Damage event
	 * @param zones Available zones
	 * @param zoneRadius Zone association radius
	 * @return Zone index if found, -1 otherwise
	 */
	int AssociateDamageWithZone(
		const StructureDamageEvent& damage,
		const std::vector<ZoneSnapshot>& zones,
		float zoneRadius) const;

	/**
	 * Calculate distance squared between two points.
	 */
	float DistanceSquared(float x1, float y1, float x2, float y2) const;
};
