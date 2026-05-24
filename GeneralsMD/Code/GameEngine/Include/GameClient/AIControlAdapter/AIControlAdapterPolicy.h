/**
 * AIControlAdapterPolicy.h
 *
 * Policy decision functions for AI Control Adapter autonomous behavior.
 *
 * This header defines the policy layer that separates strategic decision-making from
 * execution logic. All functions in this header are pure functions that:
 * - Take input structs describing current game state
 * - Return decisions (bool, const char*, or result structs)
 * - Have NO side effects
 * - Are easily testable in isolation
 *
 * Design principles:
 * 1. Separation of concerns: Policy (what to do) vs execution (how to do it)
 * 2. Testability: Pure functions with explicit inputs/outputs
 * 3. Configurability: Easy to swap policy implementations or tune parameters
 * 4. Observability: Decision reasons are returned alongside results
 *
 * Categories of policies:
 * - Production policies: When to build units, buildings, and economy structures
 * - Combat policies: When to pause production, hold army cap, queue specific units
 * - Economy policies: Opening builds, Black Market timing, eco recovery priorities
 * - Technology policies: Science and upgrade sequencing and retry logic
 * - Tactical policies: Zone front direction, radar van count, vehicle composition
 *
 * Autonomy modes:
 * The adapter supports multiple autonomy modes that use these policies:
 * - Full autonomy: Adapter controls everything (economy, combat, tactics)
 * - Partial autonomy: Adapter controls economy only, external agent controls combat
 * - Manual: External agent sends all commands, policies only used for queries
 *
 * Testing:
 * All policy functions have unit tests in AIControlAdapterPolicyTests.cpp.
 * Tests verify decision logic for various input combinations and edge cases.
 *
 * See also:
 * - AIControlAdapterPolicy.cpp for implementations
 * - AIControlAdapterAutonomy.inl for autonomy system that uses these policies
 * - Tests/AIControlAdapter/AIControlAdapterPolicyTests.cpp for test cases
 */

#pragma once

#include <vector>

#include "GameNetwork/GeneralsOnline/json.hpp"

// =============================================================================
// PRODUCTION POLICIES
// =============================================================================

/**
 * Input state for combat production pause policy.
 *
 * Used to decide whether to pause combat unit production to prioritize economy.
 */
struct AIControlAdapterProductionPolicyInputs
{
	bool isBalancedSprawl;              // True if using "Balanced Sprawl" build order
	bool openingInfrastructureReady;    // True if opening infrastructure (Barracks, Arms Dealer) complete
	bool openingEconomyReady;           // True if opening economy (initial Supply Stashes) complete
	bool wasRecoveringFromReserve;      // True if just recovered from low reserve cash
	unsigned int money;                 // Current cash balance
	unsigned int reserveCash;           // Cash reserved for emergency economy (typically $2000-$3000)
	int blackMarketsInProgress;         // Count of Black Markets currently being built
	int supplyStashesInProgress;        // Count of Supply Stashes currently being built
};

/**
 * Decide whether to pause combat unit production to focus on economy.
 *
 * This policy prevents the economy from stalling by pausing military production when:
 * - Still building opening infrastructure (first Barracks, Arms Dealer)
 * - Cash is low and opening economy isn't ready yet
 * - Recovering from reserve depletion (just built emergency economy buildings)
 * - Currently building Supply Stashes (prioritize eco completion)
 *
 * The "Balanced Sprawl" build order has stricter requirements and pauses more aggressively
 * to ensure economic foundation is solid before military buildup.
 *
 * @param inputs Current production and economy state
 * @return True if combat production should pause, false if it can continue
 */
bool AIControlAdapterShouldPauseCombatProduction(const AIControlAdapterProductionPolicyInputs& inputs);

/**
 * Input state for army cap hold policy.
 *
 * Used to decide whether to pause production when approaching unit limits.
 */
struct AIControlAdapterCombatProductionPolicyInputs
{
	bool shouldPauseForEconomy;  // True if economy policy says to pause production
	bool isBalancedSprawl;       // True if using "Balanced Sprawl" build order
	bool wasArmyCapReached;      // True if army cap was reached in previous tick
	int combatCount;             // Current count of combat units
	int armyCap;                 // Maximum desired combat units
};

/**
 * Input state for dynamic army cap policy.
 *
 * Used to raise the practical combat-unit target when the economy has scaled far
 * beyond the normal reserve and production capacity is already large.
 */
struct AIControlAdapterEffectiveArmyCapPolicyInputs
{
	bool isBalancedSprawl;       // True if using "Balanced Sprawl" build order
	unsigned int money;          // Current cash balance
	int incomePerMinute;         // Smoothed net cash change per minute
	int barracks;                // Count of Barracks
	int armsDealers;             // Count of Arms Dealers
	int baseArmyCap;             // Baseline desired combat unit cap
};

/**
 * Decide the effective combat-unit cap for the current economy state.
 *
 * Balanced Sprawl normally keeps a conservative cap to avoid pathfinding and
 * control issues. Once cash is far above reserve, that cap becomes counterproductive:
 * idle production buildings sit unused while the economy floats. In that surplus
 * mode, scale the cap with available production capacity and sustainable net cash
 * flow so late-game spending can convert into units without draining the economy.
 *
 * @param inputs Current economy and production capacity
 * @return Effective combat-unit cap
 */
int AIControlAdapterGetEffectiveArmyCap(const AIControlAdapterEffectiveArmyCapPolicyInputs& inputs);

/**
 * Decide whether to hold production at army cap limit.
 *
 * This policy prevents overproduction by stopping combat unit production when the army
 * size approaches the configured cap. The cap exists to:
 * - Prevent unit spam that degrades pathfinding performance
 * - Reserve supply limit for workers and economy units
 * - Force the player to use units in combat before building more
 *
 * Special handling:
 * - "Balanced Sprawl" continues producing even at cap (prefers larger army)
 * - Hysteresis: Once cap is reached, stays paused to prevent flip-flopping
 * - Eco pause overrides: If economy needs attention, always pause regardless of cap
 *
 * @param inputs Current combat unit count and cap settings
 * @return True if production should hold at cap, false if it can continue
 */
bool AIControlAdapterShouldHoldArmyCap(const AIControlAdapterCombatProductionPolicyInputs& inputs);

// =============================================================================
// OPENING BUILD ORDER POLICIES
// =============================================================================

/**
 * Input state for opening build order policy.
 *
 * Used to determine the next required building in the opening sequence.
 */
struct AIControlAdapterOpeningPolicyInputs
{
	int completedSupplyStashes;  // Count of completed Supply Stashes
	int completedBarracks;       // Count of completed Barracks
	int completedArmsDealers;    // Count of completed Arms Dealers
};

/**
 * Get the next required building for the opening build order.
 *
 * The opening sequence for GLA is:
 * 1. Supply Stash (initial economy)
 * 2. Barracks (infantry production)
 * 3. Supply Stash (expand economy)
 * 4. Arms Dealer (vehicle production)
 * 5. Opening complete
 *
 * This ensures:
 * - Economy comes online before military buildings
 * - Both infantry and vehicle production available early
 * - Economic expansion keeps pace with military needs
 *
 * @param inputs Current count of completed opening buildings
 * @return Building name (e.g., "GLASupplyStash") or nullptr if opening complete
 */
const char* AIControlAdapterGetRequiredOpeningBuild(const AIControlAdapterOpeningPolicyInputs& inputs);

// =============================================================================
// ECONOMY EXPANSION POLICIES
// =============================================================================

/**
 * Input state for Black Market construction policy.
 *
 * Used to decide whether to attempt building a Black Market (expensive economy building).
 */
struct AIControlAdapterBlackMarketPolicyInputs
{
	bool hasCompletedPalace;     // True if a finished Palace (tech prerequisite) exists
	bool isBalancedSprawl;       // True if using "Balanced Sprawl" build order
	unsigned int money;          // Current cash balance
	unsigned int reserveCash;    // Cash reserved for emergency economy
	int completedBlackMarkets;   // Count of completed Black Markets
	int blackMarketsInProgress;  // Count of Black Markets currently being built
};

/**
 * Decide whether to attempt building a Black Market.
 *
 * Black Markets are expensive ($1500) economy buildings that generate passive income.
 * This policy gates construction to prevent economic collapse:
 *
 * Gating conditions:
 * - Palace must exist first (tech building prerequisite)
 * - First Balanced Sprawl market can start at reserve; later markets require reserve plus cost
 * - Can only build one Black Market at a time (prevent multiple simultaneous)
 * - "Balanced Sprawl" has stricter requirements (prefers Supply Stashes first)
 *
 * Black Markets provide:
 * - Passive income generation (+$20 per collection)
 * - Access to GLA upgrades (AP bullets, worker shoes, etc.)
 * - Economic scaling in mid/late game
 *
 * @param inputs Current cash and infrastructure state
 * @return True if Black Market construction can be attempted, false otherwise
 */
bool AIControlAdapterCanAttemptBlackMarket(const AIControlAdapterBlackMarketPolicyInputs& inputs);

/**
 * Check if a build command requires careful placement timing.
 *
 * "Settling-sensitive" builds are buildings that:
 * - Need good placement (not too close to existing buildings)
 * - Should wait for workers to reach ideal positions
 * - Benefit from extra placement validation
 *
 * Examples of settling-sensitive builds:
 * - Black Markets (expensive, want good spacing)
 * - Palaces (critical tech building)
 * - Tunnels (need specific positioning for network)
 *
 * Non-settling builds (Supply Stashes, Barracks) can be placed quickly without
 * waiting for workers to settle into optimal positions.
 *
 * @param commandName Build command (e.g., "Game.BuildBlackMarket")
 * @return True if build should wait for worker settling, false for immediate placement
 */
bool AIControlAdapterIsSettlingSensitiveBuild(const char* commandName);

/**
 * Get retry delay for a failed or successful build attempt.
 *
 * Build commands can fail for various reasons (no resources, no space, worker busy).
 * This policy determines how long to wait before retrying based on:
 * - Success vs failure
 * - Failure reason (temporary vs permanent)
 * - Building type (expensive buildings get longer cooldowns)
 *
 * Retry delay examples:
 * - Success: 2000ms (give building time to start construction)
 * - Failure "no_money": 5000ms (wait for income)
 * - Failure "no_space": 10000ms (placement might be blocked)
 * - Failure "worker_busy": 1000ms (temporary state, retry soon)
 *
 * @param commandName Build command that was attempted
 * @param success True if build succeeded, false if it failed
 * @param reason Failure reason string (e.g., "no_money", "no_space")
 * @return Delay in milliseconds before next retry attempt
 */
unsigned int AIControlAdapterGetBuildRetryDelayMs(const char* commandName, bool success, const char* reason);

/**
 * Input state for economy recovery policy.
 *
 * Used when cash drops below reserve threshold to determine emergency economy builds.
 */
struct AIControlAdapterEcoRecoveryPolicyInputs
{
	bool isBalancedSprawl;               // True if using "Balanced Sprawl" build order
	int totalSupplyStashes;              // Total completed Supply Stashes
	int totalPalaces;                    // Total completed Palaces
	int totalBlackMarkets;               // Total completed Black Markets
	int desiredMarketCount;              // Target number of Black Markets for this stage
	bool shouldThrottleExtraStashGrowth; // True if should slow Supply Stash expansion
	bool canAttemptBlackMarket;          // True if Black Market policy allows building one
};

/**
 * Get the next economy building to construct during cash crisis recovery.
 *
 * When cash drops below the reserve threshold (typically $2000-$3000), the autonomy
 * system enters "eco recovery mode" and prioritizes economy buildings over combat.
 * This policy determines which economy building to construct:
 *
 * Priority order:
 * 1. Palace - if none exists (unlocks Black Markets and technologies)
 * 2. Black Market - if below desired count and policy allows
 * 3. Supply Stash - default economy building
 *
 * Throttling:
 * - "shouldThrottleExtraStashGrowth" limits Supply Stash spam in late game
 * - "Balanced Sprawl" prefers Black Markets over endless Supply Stashes
 *
 * Special cases:
 * - Early game: Builds Supply Stashes (cheap, fast income)
 * - Mid game: Adds Black Markets (passive income scaling)
 * - Late game: Balances both types based on income needs
 *
 * @param inputs Current economy infrastructure state
 * @return Building name (e.g., "GLASupplyStash") or nullptr if no eco build needed
 */
const char* AIControlAdapterGetEcoRecoveryBuild(const AIControlAdapterEcoRecoveryPolicyInputs& inputs);

// =============================================================================
// TECHNOLOGY POLICIES (Science & Upgrades)
// =============================================================================

/**
 * Decide whether to abort the current science research for this tick.
 *
 * Science research (General's Powers like Scud Storm, Cash Bounty) is expensive and
 * can stall the economy if attempted at the wrong time. This policy aborts research when:
 * - "no_money": Not enough cash (temporary state, will retry)
 * - "no_producer": Palace destroyed (permanent failure, abort plan)
 * - "producer_busy": Palace researching something else (conflict, abort)
 *
 * Aborting vs retrying:
 * - Abort: Stop trying this science, move to next in plan
 * - Retry: Temporary failure, try again next tick or after delay
 *
 * @param reason Failure reason from science purchase attempt
 * @return True if science plan should abort (permanent failure), false to retry
 */
bool AIControlAdapterShouldAbortSciencePlanForTick(const char* reason);

/**
 * Decide whether to abort the remaining upgrade plan for this tick.
 *
 * Upgrades (AP bullets, worker shoes, junk repair) improve units and buildings.
 * This policy aborts remaining upgrade attempts when:
 * - "no_producer": Required building destroyed (e.g., Black Market for AP bullets)
 * - "producer_busy": Building busy with another upgrade
 * - "already_purchased": Upgrade already owned (shouldn't happen, but safe to abort)
 *
 * Failures that should not abort the whole plan:
 * - "no_money": Not enough cash (will retry when money available)
 * - "producer_cannot_make_upgrade": Current producer cannot make this upgrade;
 *   continue so later upgrades can still be considered.
 * - "upgrade_already_in_production": Current upgrade is already underway;
 *   continue scanning so later upgrades are not hidden behind it.
 *
 * @param reason Failure reason from upgrade purchase attempt
 * @return True if remaining upgrade plan should abort, false to continue scanning
 */
bool AIControlAdapterShouldAbortUpgradePlanForTick(const char* reason);

/**
 * Get retry delay for a failed or successful science/upgrade purchase.
 *
 * Technology purchases (science research, upgrades) have longer retry delays than
 * unit production because:
 * - They're expensive (don't spam retry and drain cash)
 * - They're one-time purchases (no urgency)
 * - Building busy states last longer (research takes time)
 *
 * Retry delay examples:
 * - Success: 5000ms (give research time to complete)
 * - Failure "no_money": 10000ms (wait for significant income)
 * - Failure "producer_busy": 5000ms (wait for current research to finish)
 *
 * @param issued True if purchase succeeded, false if it failed
 * @param reason Failure reason string (e.g., "no_money", "producer_busy")
 * @return Delay in milliseconds before next retry attempt
 */
unsigned int AIControlAdapterGetTechRetryDelayMs(bool issued, const char* reason);

/**
 * Get retry delay for a failed or successful unit/building production command.
 *
 * Production commands (build unit, build building) have shorter retry delays than
 * technology because:
 * - They're recurring (need continuous production)
 * - They're cheaper (can retry more frequently)
 * - Building busy states are shorter (unit production is faster)
 *
 * Retry delay examples:
 * - Success: 500ms (queue next unit quickly)
 * - Failure "no_money": 2000ms (wait for income)
 * - Failure "queue_full": 5000ms (wait for queue space)
 * - Failure "no_producer": 10000ms (wait for building to be rebuilt)
 *
 * @param issued True if production succeeded, false if it failed
 * @param reason Failure reason string (e.g., "no_money", "queue_full")
 * @return Delay in milliseconds before next retry attempt
 */
unsigned int AIControlAdapterGetProductionRetryDelayMs(bool issued, const char* reason);

// =============================================================================
// UNIT COMPOSITION POLICIES
// =============================================================================

/**
 * Input state for Radar Van production policy.
 *
 * Used to decide whether to build Radar Vans (stealth detection vehicles).
 */
struct AIControlAdapterRadarVanPolicyInputs
{
	bool shouldPauseForEconomy;  // True if economy policy says to pause production
	bool shouldHoldArmyCap;      // True if at army cap limit
	int armsDealers;             // Count of Arms Dealers (build Radar Vans)
	int radarVans;               // Current count of Radar Vans
	int combatVehicles;          // Current count of combat vehicles (Quads, Scorpions, etc.)
	int minRadarVans;            // Minimum desired Radar Vans (typically 1-2)
};

/**
 * Decide whether to queue a Radar Van for production.
 *
 * Radar Vans provide stealth detection, revealing invisible units. The policy ensures:
 * - Minimum coverage: Always have at least minRadarVans (typically 1-2)
 * - Ratio maintenance: Keep ~1 Radar Van per 10 combat vehicles
 * - Production capacity: Only build if Arms Dealer exists
 * - Resource gating: Respect economy pause and army cap limits
 *
 * Radar Van importance:
 * - Counter stealth units (Stealth Fighters, Tunnel Networks)
 * - Reveal mines and traps
 * - Support army with vision in fog of war
 *
 * Production is paused if:
 * - Economy needs attention (shouldPauseForEconomy)
 * - At army cap limit (shouldHoldArmyCap)
 * - No Arms Dealer available
 *
 * @param inputs Current Radar Van count and production capacity
 * @return True if should queue Radar Van, false otherwise
 */
bool AIControlAdapterShouldQueueRadarVan(const AIControlAdapterRadarVanPolicyInputs& inputs);

// =============================================================================
// TIMING UTILITIES
// =============================================================================

/**
 * Check if a deadline tick has elapsed (handles 32-bit unsigned integer wraparound).
 *
 * Game tick counters are 32-bit unsigned integers that wrap around after ~49 days of
 * gameplay at 30 FPS. This function correctly handles wraparound comparison.
 *
 * Example:
 * - now = 100, deadline = 50 -> true (deadline passed)
 * - now = 100, deadline = 150 -> false (deadline in future)
 * - now = 0xFFFFFF00, deadline = 0x00000100 -> false (wraparound, deadline in future)
 *
 * @param deadline Target tick counter
 * @param now Current tick counter
 * @return True if deadline has passed or is now, false if deadline is in future
 */
bool AIControlAdapterHasTickElapsed(unsigned int deadline, unsigned int now);

/**
 * Check if a deadline tick is in the future (handles 32-bit unsigned integer wraparound).
 *
 * Inverse of AIControlAdapterHasTickElapsed(). Returns true if deadline has not yet
 * been reached.
 *
 * @param deadline Target tick counter
 * @param now Current tick counter
 * @return True if deadline is in future, false if deadline has passed or is now
 */
bool AIControlAdapterIsTickInFuture(unsigned int deadline, unsigned int now);

// =============================================================================
// PRODUCTION DECISION POLICIES
// =============================================================================

/**
 * Input state for production choice policy.
 *
 * Comprehensive state snapshot for deciding which unit to build next.
 */
struct AIControlAdapterProductionChoiceInputs
{
	bool shouldPauseForEconomy;  // True if economy policy says to pause production
	const char* pauseReason;     // Reason for pause (e.g., "opening_incomplete")
	bool shouldHoldArmyCap;      // True if at army cap limit
	bool isBalancedSprawl;       // True if using "Balanced Sprawl" build order
	const char* profile;         // Production profile (e.g., "aggressive", "defensive")
	unsigned int money;          // Current cash balance
	int barracks;                // Count of Barracks (build infantry)
	int armsDealers;             // Count of Arms Dealers (build vehicles)
	int palaces;                 // Total count of Palaces, including in-progress
	bool hasCompletedPalace;     // True if a finished Palace exists for live unit prerequisites
	bool hasScudLauncherScience; // Whether the player has purchased SCIENCE_ScudLauncher
	bool hasCaptureUpgrade;      // Whether infantry capture upgrade is complete
	int captureSources;          // Current count of live units with capture power
	int soldiers;                // Current count of Rebel/Worker infantry
	int rpg;                     // Current count of RPG Troopers
	int quads;                   // Current count of Quads (fast attack vehicle)
	int scorpions;               // Current count of Scorpion Tanks
	int scudLaunchers;           // Current count of Scud Launchers (artillery)
	int radarVans;               // Current count of Radar Vans
	int armyCount;               // Total combat unit count
	int armyCap;                 // Maximum desired combat units
};

/**
 * Result of production choice policy decision.
 */
struct AIControlAdapterProductionChoiceResult
{
	const char* command;  // Command to execute (e.g., "Game.BuildWorker") or nullptr
	const char* reason;   // Reason for choice (e.g., "opening_soldiers", "pause_for_economy")
};

/**
 * Choose the next unit to produce based on current game state and production priorities.
 *
 * This is the core production policy that decides which unit to build each tick during
 * autonomous combat production. It implements a priority system:
 *
 * Priority 1: Production gates
 * - Pause if economy needs attention
 * - Hold if at army cap
 * - Skip if no production buildings
 *
 * Priority 2: Early game composition (opening)
 * - Ensure minimum soldiers (4-6) before vehicles
 * - Build initial RPG troopers (anti-vehicle)
 * - Establish infantry backbone
 *
 * Priority 3: Vehicle production (mid/late game)
 * - Maintain vehicle/infantry ratio (prefer vehicles)
 * - Queue Scud Launchers if Palace exists
 * - Build Scorpion Tanks (main battle tank)
 * - Build Quads (fast raider)
 *
 * Priority 4: Special units
 * - Maintain Radar Van count (stealth detection)
 * - Fill gaps in composition
 *
 * Build order profiles:
 * - "aggressive": Favors Quads and early vehicle rush
 * - "balanced": Mix of infantry and vehicles
 * - "defensive": More RPG troopers and Scorpions
 *
 * The policy returns both:
 * - command: What to build (e.g., "Game.BuildWorker", "Game.BuildScorpion")
 * - reason: Why it chose that unit (for debugging and learning)
 *
 * @param inputs Current game state, unit counts, and production capacity
 * @return Result with command to execute and reason for choice
 */
AIControlAdapterProductionChoiceResult AIControlAdapterChoosePreferredProductionCommand(
	const AIControlAdapterProductionChoiceInputs& inputs);

/**
 * Input state for vehicle replenishment priority policy.
 *
 * Used to decide whether to prioritize vehicle production over infantry.
 */
struct AIControlAdapterVehicleSustainPolicyInputs
{
	bool isBalancedSprawl;  // True if using "Balanced Sprawl" build order
	int armsDealers;        // Count of Arms Dealers (build vehicles)
	int barracks;           // Count of Barracks (build infantry)
	int quads;              // Current count of Quads
	int scorpions;          // Current count of Scorpion Tanks
	int scudLaunchers;      // Current count of Scud Launchers
	int radarVans;          // Current count of Radar Vans
	int soldiers;           // Current count of Rebel/Worker infantry
	int rpg;                // Current count of RPG Troopers
};

/**
 * Decide whether to prioritize vehicle production over infantry.
 *
 * GLA strategy depends on maintaining a good vehicle/infantry ratio. This policy
 * determines when to focus on vehicles:
 *
 * Prioritize vehicles when:
 * - Have Arms Dealer but vehicle count is low
 * - Vehicle losses need replenishment (army composition imbalanced)
 * - Infantry count is sufficient (4+ soldiers, 2+ RPG)
 *
 * Prioritize infantry when:
 * - No Arms Dealer exists yet
 * - Infantry count too low (need backbone)
 * - Vehicle production capacity limited
 *
 * "Balanced Sprawl" build order favors vehicles more aggressively to enable
 * early map control and economic expansion.
 *
 * @param inputs Current unit composition and production capacity
 * @return True if should prioritize vehicle production, false for infantry focus
 */
bool AIControlAdapterShouldPreferVehicleReplenishment(const AIControlAdapterVehicleSustainPolicyInputs& inputs);

// =============================================================================
// MACRO (BASE BUILDING) POLICIES
// =============================================================================

/**
 * Input state for macro completion detection policy.
 *
 * Used to decide whether base building ("macro") is complete enough to focus on combat.
 */
struct AIControlAdapterMacroCompletionPolicyInputs
{
	bool isSprawlStyle;            // True if using sprawl build order (multiple bases)
	bool shouldForceEcoRecovery;   // True if economy is in crisis (need more buildings)
	bool remoteZoneNeedsFollowup;  // True if remote expansion needs more buildings
	int totalSupplyStashes;        // Total completed Supply Stashes
	int supplyCap;                 // Target Supply Stash count for current stage
	int totalBarracks;             // Total completed Barracks
	int barracksCap;               // Target Barracks count
	int totalArmsDealers;          // Total completed Arms Dealers
	int armsCap;                   // Target Arms Dealer count
	int totalBlackMarkets;         // Total completed Black Markets
	int marketCap;                 // Target Black Market count
	int totalTunnels;              // Total completed Tunnels (expansions)
	int tunnelCap;                 // Target Tunnel count
	int totalStingers;             // Total completed Stinger Sites (anti-air)
	int stingerCap;                // Target Stinger Site count
};

/**
 * Decide whether macro (base building) is complete enough to focus on army/combat.
 *
 * "Macro" refers to base building: economy, production buildings, defenses. This policy
 * determines when the base is "good enough" to shift focus to army production and combat:
 *
 * Macro is complete when:
 * - All infrastructure at or above target caps
 * - No eco recovery needed
 * - Remote expansions don't need followup
 *
 * Macro is incomplete when:
 * - Missing critical buildings (below caps)
 * - Economy in crisis (shouldForceEcoRecovery)
 * - Remote zone needs more infrastructure
 *
 * Special handling for "sprawl style":
 * - More lenient completion requirements
 * - Accepts partial completion to push aggression
 * - Prioritizes expansion over perfection
 *
 * This policy prevents:
 * - Overbuilding base while army starves
 * - Rushing army before economy can sustain it
 * - Neglecting expansions that need infrastructure
 *
 * @param inputs Current infrastructure counts vs target caps
 * @return True if macro is complete (focus on army), false if more building needed
 */
bool AIControlAdapterShouldTreatMacroAsComplete(const AIControlAdapterMacroCompletionPolicyInputs& inputs);

/**
 * Input state for Black Market growth priority policy.
 *
 * Used to decide whether to prioritize Black Markets over production buildings.
 */
struct AIControlAdapterMarketGrowthPolicyInputs
{
	bool isBalancedSprawl;                // True if using "Balanced Sprawl" build order
	bool shouldForceEcoRecovery;          // True if economy is in crisis
	bool shouldPrioritizeMarketGrowth;    // True if market scaling phase active
	bool canAttemptBlackMarket;           // True if Black Market policy allows building one
	bool shouldPreserveReserve;           // True if should maintain cash reserve
	bool canScaleMilitaryProduction;      // True if have capacity for more production buildings
	bool isSprawlStyle;                   // True if using sprawl build order
	int totalBlackMarkets;                // Total completed Black Markets
	int desiredMarketCount;               // Target Black Market count for current stage
	int totalBarracks;                    // Total completed Barracks
	int barracksCap;                      // Target Barracks count
	int totalArmsDealers;                 // Total completed Arms Dealers
	int armsCap;                          // Target Arms Dealer count
};

/**
 * Decide whether to prioritize Black Markets over production buildings (Barracks, Arms Dealers).
 *
 * This policy determines the economic scaling strategy - whether to invest in:
 * - Black Markets: Passive income (long-term investment, scales economy)
 * - Production buildings: More unit production capacity (immediate army strength)
 *
 * Prioritize Black Markets when:
 * - Below desired market count (scaling phase)
 * - Market growth phase active (mid-game economic expansion)
 * - Economy is healthy (not in crisis recovery)
 * - Already have sufficient production capacity
 *
 * Prioritize production buildings when:
 * - Economy in crisis (need immediate productivity)
 * - Military production capacity limited
 * - Market count at or above target
 * - "Balanced Sprawl" in aggressive mode
 *
 * This creates distinct phases:
 * - Early game: Production buildings (enable army)
 * - Mid game: Black Markets (scale income)
 * - Late game: Balance both based on needs
 *
 * @param inputs Current infrastructure and scaling phase state
 * @return True if should prioritize markets, false for production buildings
 */
bool AIControlAdapterShouldPrioritizeMarketsOverProductionBuildings(
	const AIControlAdapterMarketGrowthPolicyInputs& inputs);

/**
 * Input state for zone expansion urgency policy.
 *
 * Used to decide whether zone expansion should override market growth priority.
 */
struct AIControlAdapterZoneExpansionPolicyInputs
{
	int currentZoneCount;   // Current number of developed zones
	int desiredZoneCount;   // Target zone count for current phase
	int zoneGapThreshold;   // Minimum gap to consider expansion urgent (typically 5)
};

/**
 * Decide whether zone expansion is urgent and should override market growth.
 *
 * When the bot is significantly below its desired zone count, territorial expansion
 * should take priority over economic optimization (Black Markets). This prevents
 * the bot from getting stuck attempting failed Black Market placements when it
 * should be expanding via Supply Stashes instead.
 *
 * Zone expansion is urgent when:
 * - Current zone count is below desired count
 * - The gap is >= threshold (typically 5 zones)
 *
 * This ensures:
 * - Bot doesn't get stuck at low zone counts
 * - Territorial expansion takes priority when significantly behind target
 * - Economic optimization (markets) only happens when sprawl is progressing
 *
 * Related bugs:
 * - B031: Zone expansion blocked by failed Black Market placement attempts
 *
 * @param inputs Current and desired zone counts
 * @return True if zone expansion is urgent and should override markets
 */
bool AIControlAdapterIsZoneExpansionUrgent(const AIControlAdapterZoneExpansionPolicyInputs& inputs);

/**
 * Decide whether to abort upgrade plan based on failure reason.
 *
 * Similar to AIControlAdapterShouldAbortUpgradePlanForTick() but takes reason as input.
 * Used for policy queries without tick state.
 *
 * @param reason Failure reason from upgrade purchase attempt
 * @return True if should abort (permanent failure), false to retry
 */
bool AIControlAdapterShouldAbortUpgradePlanForReason(const char* reason);

// =============================================================================
// TACTICAL & GEOMETRIC POLICIES
// =============================================================================

/**
 * 2D map position (x, y coordinates).
 */
struct AIControlAdapterMapPoint
{
	float x;  // X coordinate (horizontal)
	float y;  // Y coordinate (vertical)
};

/**
 * Normalize a 2D direction vector to unit length.
 *
 * Takes direction components (dx, dy) and normalizes to unit vector. Handles zero-length
 * vectors safely by returning false.
 *
 * @param dx X component of direction vector
 * @param dy Y component of direction vector
 * @param outDx Output: normalized X component (unit length)
 * @param outDy Output: normalized Y component (unit length)
 * @return True if normalization succeeded, false if vector has zero length
 */
bool AIControlAdapterTryNormalizeDirection(float dx, float dy, float& outDx, float& outDy);

/**
 * Parse a map position from JSON message.
 *
 * Reads {"x": N, "y": M} JSON object and converts to AIControlAdapterMapPoint.
 *
 * @param mapPos JSON object containing "x" and "y" fields
 * @param outPos Output: parsed map position
 * @return True if parsing succeeded, false if JSON malformed or missing fields
 */
bool AIControlAdapterTryReadMapPosition(const nlohmann::json& mapPos, AIControlAdapterMapPoint& outPos);

/**
 * Extract most recent attack location from event history.
 *
 * Searches event history for attack events within the freshness window and returns
 * the position of the most recent attack. Used to direct counterattacks.
 *
 * @param events JSON array of game events (from adapter event system)
 * @param nowTick Current game tick counter
 * @param freshnessMs Maximum age of attack event to consider (in milliseconds)
 * @param outPos Output: position of most recent attack
 * @return True if recent attack found, false if no attacks within freshness window
 */
bool AIControlAdapterTryGetRecentAttackTarget(
	const nlohmann::json& events,
	unsigned int nowTick,
	unsigned int freshnessMs,
	AIControlAdapterMapPoint& outPos);

/**
 * Result of zone front direction resolution.
 */
struct AIControlAdapterZoneFrontDirectionResult
{
	float dx;              // Normalized direction X component
	float dy;              // Normalized direction Y component
	const char* source;    // Source of direction (e.g., "recent_attack", "enemy_base", "fallback")
};

/**
 * Determine the "front" direction for a zone (where enemies are likely located).
 *
 * Zones are spatial regions where the AI maintains army presence. This policy determines
 * which direction the "front" faces - where to send offensive units and face defenses.
 *
 * Priority order for direction sources:
 * 1. Recent attack: If zone was attacked recently, face that direction (counterattack)
 * 2. Preferred enemy base: If know main enemy location, face that direction
 * 3. Any known enemy base: Face nearest known enemy position
 * 4. Fallback direction: Use provided default (e.g., map center, spawn direction)
 *
 * This enables:
 * - Dynamic response to attacks (turn to face attackers)
 * - Offensive posture toward known enemies
 * - Reasonable defaults when enemy locations unknown
 *
 * Used for:
 * - Positioning buildings (face defenses toward front)
 * - Rally point placement (rear = safe, front = aggressive)
 * - Attack move directions (push toward front)
 *
 * @param zoneCenter Center point of the zone
 * @param hasRecentAttackTarget True if recent attack detected
 * @param recentAttackTarget Position of recent attack (if hasRecentAttackTarget)
 * @param hasPreferredEnemyBase True if preferred enemy base known
 * @param preferredEnemyBase Position of preferred enemy base (if hasPreferredEnemyBase)
 * @param knownEnemyBases Vector of all known enemy base positions
 * @param fallbackDx Fallback direction X component (if no better source available)
 * @param fallbackDy Fallback direction Y component (if no better source available)
 * @return Direction result with normalized direction and source reason
 */
AIControlAdapterZoneFrontDirectionResult AIControlAdapterResolveZoneFrontDirection(
	const AIControlAdapterMapPoint& zoneCenter,
	bool hasRecentAttackTarget,
	const AIControlAdapterMapPoint& recentAttackTarget,
	bool hasPreferredEnemyBase,
	const AIControlAdapterMapPoint& preferredEnemyBase,
	const std::vector<AIControlAdapterMapPoint>& knownEnemyBases,
	float fallbackDx,
	float fallbackDy);

/**
 * Front and rear rally points for a zone.
 */
struct AIControlAdapterZoneFrontRearPoints
{
	AIControlAdapterMapPoint frontPoint;  // Offensive rally point (toward enemy)
	AIControlAdapterMapPoint rearPoint;   // Defensive rally point (toward base)
};

/**
 * Build front and rear rally points for a zone based on direction and radius.
 *
 * Given a zone center, radius, and front direction, calculates two rally points:
 * - Front point: radius * direction from center (offensive staging area)
 * - Rear point: radius * -direction from center (defensive fallback position)
 *
 * Rally points are used for:
 * - Unit grouping before attacks (front point)
 * - Retreat destination during defense (rear point)
 * - Production building rally points (front for combat, rear for workers)
 *
 * Example:
 * - Center: (100, 100), radius: 50, direction: (1, 0) [east]
 * - Front point: (150, 100) [east of center]
 * - Rear point: (50, 100) [west of center]
 *
 * @param center Zone center position
 * @param radius Distance from center to rally points
 * @param dirDx Front direction X component (normalized)
 * @param dirDy Front direction Y component (normalized)
 * @return Structure with frontPoint and rearPoint positions
 */
AIControlAdapterZoneFrontRearPoints AIControlAdapterBuildZoneFrontRearPoints(
	const AIControlAdapterMapPoint& center,
	float radius,
	float dirDx,
	float dirDy);
