/**
 * AIControlAdapterProductionManager.h
 *
 * Production management system for AI Control Adapter autonomous behavior.
 *
 * This module extracts combat unit production decision logic from the main autonomy loop to provide:
 * - Income-aware army cap scaling
 * - Late-game surplus production pressure
 * - Zone-preferred producer selection
 * - Nearest eligible producer fallback
 * - Producer queue-full continuation
 * - Explicit production decisions for telemetry
 *
 * Design principles:
 * 1. Ownership: ProductionManager owns combat unit production decisions
 * 2. Separation: Read game state from inputs, emit production intents, don't execute commands
 * 3. Observability: Production decisions are explicit and loggable
 * 4. Testability: Decision logic can be tested without launching the game
 *
 * Phase 2 scope (architecture-improvement-plan-codex-2026-05-26.md):
 * - Extract combat unit production decisions only
 * - Preserve existing command execution paths
 * - Use existing snapshot/count inputs
 * - Do NOT absorb economy, macro building, or worker production
 *
 * Related bugs fixed:
 * - Late-game cash float (high cash + positive income should increase production)
 * - Poor use of available Barracks/Arms Dealers (queue-full on one should not block others)
 * - Weak zone-local production after attacks
 */

#pragma once

#include <string>
#include <vector>

// Forward declarations
class Object;

/**
 * Production intent returned by ProductionManager.
 *
 * Describes what unit to produce, which producer to use, and why.
 * The adapter executes this intent through existing command paths.
 */
struct ProductionIntent
{
	std::string unitTemplate;       // Unit template name (e.g., "GLAVehicleQuad") or empty if no production
	std::string producerKind;       // "barracks", "arms_dealer", or "any"
	int producerObjectId;           // Specific producer ID if zone-preferred, or -1 for "any"
	std::string commandName;        // Command name for logging (e.g., "Game.QueueQuadsAllWarFactories")
	std::string reason;             // Why production chosen or skipped (e.g., "ok", "army_cap_reached", "no_money")
	bool shouldProduce;             // True if production should be attempted

	ProductionIntent()
		: producerObjectId(-1)
		, shouldProduce(false)
	{}
};

/**
 * Owned object snapshot for producer selection.
 *
 * Minimal subset of AutonomyOwnedObjectSnapshot needed for zone-preferred producer selection.
 */
struct ProductionProducerSnapshot
{
	Object* object;          // Object pointer (for template inference, not stored in intent)
	int objectId;            // Object ID (stored in intent for command execution)
	bool isStructure;
	bool underConstruction;
	bool isBarracks;
	bool isWarFactoryLike;  // Arms Dealer or War Factory
	float positionX;
	float positionY;

	ProductionProducerSnapshot()
		: object(nullptr)
		, objectId(-1)
		, isStructure(false)
		, underConstruction(false)
		, isBarracks(false)
		, isWarFactoryLike(false)
		, positionX(0.0f)
		, positionY(0.0f)
	{}
};

/**
 * Inputs for production decision.
 *
 * Aggregates all state needed to make production decisions without maintaining duplicate world state.
 */
struct ProductionManagerInputs
{
	// Financial state
	unsigned int money;
	int incomePerMinute;
	unsigned int reserveCash;

	// Unit counts
	int armyCount;           // Total combat units (mobileUnits - workers)
	int soldiers;
	int rpg;
	int quads;
	int scorpions;
	int scudLaunchers;
	int radarVans;

	// Producer counts
	int barracks;
	int armsDealers;
	int blackMarkets;
	int palaces;
	bool hasCompletedPalace;

	// Queued counts
	int queuedProductionEntries;
	int queuedQuads;
	int queuedScorpions;
	int queuedScudLaunchers;

	// Capabilities
	bool hasScudLauncherScience;
	bool hasCaptureUpgrade;
	int captureSources;  // Legacy: total live capture sources (kept for compatibility)

	// Phase 6.3: Capture source capacity tracking
	int captureSourcesLive;             // Total capture-capable units alive
	int captureSourcesReserved;         // Capture sources in active capture tasks
	int captureSourcesAvailable;        // live - reserved
	int capturableTargetsRemaining;     // Capturable structures not friendly, not reserved
	int desiredCaptureSources;          // Target reserve: min(maxConcurrent + 2, targets)
	int maxCaptureConcurrent;           // Max concurrent capture tasks from automation config

	// Zone info
	bool hasActiveZone;
	float activeZoneCenterX;
	float activeZoneCenterY;
	float zoneRadius;

	// Opening state
	bool openingInfrastructureReady;
	bool openingEconomyReady;
	bool wasRecoveringFromReserve;
	bool wasArmyCapReached;

	// Profile
	const char* profile;
	bool isBalancedSprawl;
	bool surplusProductionPressure;  // True when cash >> reserve and can afford aggressive production

	// Producers (for zone selection)
	const std::vector<ProductionProducerSnapshot>* ownedProducers;

	ProductionManagerInputs()
		: money(0)
		, incomePerMinute(0)
		, reserveCash(0)
		, armyCount(0)
		, soldiers(0)
		, rpg(0)
		, quads(0)
		, scorpions(0)
		, scudLaunchers(0)
		, radarVans(0)
		, barracks(0)
		, armsDealers(0)
		, blackMarkets(0)
		, palaces(0)
		, hasCompletedPalace(false)
		, queuedProductionEntries(0)
		, queuedQuads(0)
		, queuedScorpions(0)
		, queuedScudLaunchers(0)
		, hasScudLauncherScience(false)
		, hasCaptureUpgrade(false)
		, captureSources(0)
		, captureSourcesLive(0)
		, captureSourcesReserved(0)
		, captureSourcesAvailable(0)
		, capturableTargetsRemaining(0)
		, desiredCaptureSources(0)
		, maxCaptureConcurrent(0)
		, hasActiveZone(false)
		, activeZoneCenterX(0.0f)
		, activeZoneCenterY(0.0f)
		, zoneRadius(0.0f)
		, openingInfrastructureReady(false)
		, openingEconomyReady(false)
		, wasRecoveringFromReserve(false)
		, wasArmyCapReached(false)
		, profile(nullptr)
		, isBalancedSprawl(false)
		, surplusProductionPressure(false)
		, ownedProducers(nullptr)
	{}
};

struct RadarVanProductionInputs
{
	unsigned int money = 0;
	unsigned int reserveCash = 0;
	int supplyStashes = 0;
	int barracks = 0;
	int blackMarkets = 0;
	int armsDealers = 0;
	int radarVans = 0;
	int combatVehicles = 0;
	int minRadarVans = 1;
	int armyCap = 100;
	bool isBalancedSprawl = false;
	bool wasRecoveringFromReserve = false;
	bool wasArmyCapReached = false;
};

struct RadarVanProductionDecision
{
	bool shouldQueue = false;
	bool shouldPauseCombatProduction = false;
	bool shouldHoldArmyCap = false;
	const char* reason = "not_needed";
};

/**
 * Production manager for autonomous combat unit production.
 *
 * Manages production cap scaling, unit mix decisions, and producer selection.
 * Extracted from AIControlAdapter main loop to provide clear production ownership.
 */
class AIControlAdapterProductionManager
{
public:
	AIControlAdapterProductionManager();

	/**
	 * Choose combat unit production for this tick.
	 *
	 * Determines whether to produce, what unit to produce, and which producer to use.
	 * Returns a production intent that the adapter executes through existing command paths.
	 *
	 * Decision process:
	 * 1. Calculate effective army cap based on cash, income, and producer capacity
	 * 2. Check if production should be paused (reserve recovery, opening economy)
	 * 3. Check if army cap has been reached
	 * 4. Select preferred unit based on unit counts and capabilities
	 * 5. Select producer (zone-preferred if available, fallback to "all" command)
	 * 6. Handle queue-full continuation to alternative units/producers
	 *
	 * @param inputs Current game state for production decisions
	 * @param currentTick Current game tick for telemetry
	 * @return Production intent describing what to produce and where
	 */
	ProductionIntent ChooseProduction(
		const ProductionManagerInputs& inputs,
		unsigned int currentTick);

	RadarVanProductionDecision ChooseRadarVanProduction(const RadarVanProductionInputs& inputs) const;

	/**
	 * Reset production manager state (called when starting new game).
	 */
	void Reset();

private:
	/**
	 * Calculate effective combat-unit cap for current economy state.
	 *
	 * At high cash + positive income, cap rises above base to use idle producers.
	 * At high cash + weak/negative income, cap stays conservative to avoid drain.
	 */
	int CalculateEffectiveArmyCap(const ProductionManagerInputs& inputs);

	/**
	 * Check if combat production should be paused for economy/opening.
	 */
	bool ShouldPauseCombatProduction(const ProductionManagerInputs& inputs);

	/**
	 * Check if production should hold at army cap limit.
	 */
	bool ShouldHoldArmyCap(const ProductionManagerInputs& inputs, int armyCap);

	/**
	 * Choose preferred unit to produce based on current unit counts and capabilities.
	 *
	 * Returns command name like "Game.QueueQuadsAllWarFactories" or nullptr if no production.
	 */
	const char* ChoosePreferredUnit(const ProductionManagerInputs& inputs, int armyCap);

	/**
	 * Select a zone-preferred producer for the given unit.
	 *
	 * If active zone has eligible producers, selects the nearest one.
	 * Returns true and fills outIntent with producer details if successful.
	 */
	bool SelectZonePreferredProducer(
		const ProductionManagerInputs& inputs,
		bool wantBarracks,
		ProductionIntent& outIntent);

	// No state - ProductionManager is stateless and makes decisions from inputs
};
