/**
 * AIControlAdapterScheduler.h
 *
 * Intent scheduling system for AI Control Adapter autonomous behavior.
 *
 * This module introduces an intent queue between managers and command execution to:
 * - Prioritize competing actions from different managers
 * - Enforce per-category attempt limits per tick
 * - Prevent one failed action from blocking unrelated work
 * - Unify result telemetry across all manager actions
 *
 * Design principles:
 * 1. Separation: Managers emit intents, Scheduler arbitrates, Adapter executes
 * 2. Priority: Higher-priority intents are attempted first
 * 3. Budgets: Category limits prevent one manager from dominating the tick
 * 4. Isolation: Failed intents don't poison unrelated intents
 * 5. Observability: All intent attempts are logged with structured results
 *
 * Phase 4 scope (architecture-improvement-plan-codex-2026-05-26.md):
 * - Schedule existing manager outputs: tech, production, defense production
 * - Convert manager outputs to lightweight intents before execution
 * - Keep existing game command execution helpers intact
 * - Keep TechManager, ProductionManager, ZoneManager, DefenseManager ownership intact
 * - Do NOT convert macro building, worker economy, capture, combat yet
 *
 * Expected bug impact:
 * - Reduces hidden coupling between macro, production, and tech
 * - Makes command failures durable and queryable
 * - Failed tech attempts don't block production when budget allows
 * - Failed production attempts don't poison unrelated intents
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

/**
 * Intent category for budget enforcement and telemetry.
 *
 * Each category can have different attempt limits per tick.
 */
enum class IntentCategory
{
	TECH_SCIENCE,      // Science purchases
	TECH_UPGRADE,      // Upgrade purchases
	PRODUCTION,        // Combat unit production
	DEFENSE,           // Defense production (subset of production, higher priority)
	DEFENSE_RESPONSE,  // Zone defense mobilization (Phase 7: immediate unit response to threatened zones)
	MACRO,             // Building construction (future)
	ECONOMY,           // Worker production/assignment (future)
	COMBAT,            // Unit movement/attack (future)
};

/**
 * Intent priority for arbitration.
 *
 * Higher-priority intents are attempted before lower-priority intents
 * within the same category budget.
 */
enum class IntentPriority
{
	CRITICAL = 0,      // Defense, emergency response
	HIGH = 1,          // Tech, important production
	NORMAL = 2,        // Standard production, macro
	LOW = 3,           // Opportunistic actions, low-value work
};

/**
 * Intent execution state.
 */
enum class IntentState
{
	PENDING,           // Intent not yet attempted
	ATTEMPTED,         // Intent attempted, may have succeeded or failed
	SKIPPED,           // Intent skipped due to budget/priority
};

/**
 * Lightweight intent for scheduler arbitration.
 *
 * Represents a single action that a manager wants to execute.
 * Scheduler decides which intents to attempt based on priority and category budgets.
 */
struct Intent
{
	IntentCategory category;     // Intent category for budget enforcement
	IntentPriority priority;     // Intent priority for arbitration
	std::string commandName;     // Command name (e.g., "Game.PurchaseScience")
	std::string targetName;      // Target name (e.g., science/upgrade/unit name)
	std::string reason;          // Why this intent was created (for telemetry)
	int producerObjectId;        // Producer object ID if applicable, -1 otherwise
	std::string producerKind;    // Producer kind if applicable (e.g., "barracks")

	// Execution function: returns true if command issued, false if skipped/failed
	// Updates resultReason with success/failure reason
	std::function<bool(std::string& resultReason)> executeFunc;

	Intent()
		: category(IntentCategory::PRODUCTION)
		, priority(IntentPriority::NORMAL)
		, producerObjectId(-1)
	{}
};

/**
 * Intent execution result.
 *
 * Records what happened when an intent was attempted.
 */
struct IntentResult
{
	IntentCategory category;     // Intent category
	IntentPriority priority;     // Intent priority
	IntentState state;           // Execution state (attempted, skipped)
	std::string commandName;     // Command name
	std::string targetName;      // Target name
	std::string reason;          // Why intent was created
	std::string resultReason;    // Result of execution (success/failure reason)
	bool attempted;              // True if execute function was called
	bool issued;                 // True if command was successfully issued
	unsigned int tick;           // Tick when intent was processed
	int producerObjectId;        // Producer object ID if applicable
	std::string producerKind;    // Producer kind if applicable

	IntentResult()
		: category(IntentCategory::PRODUCTION)
		, priority(IntentPriority::NORMAL)
		, state(IntentState::PENDING)
		, attempted(false)
		, issued(false)
		, tick(0)
		, producerObjectId(-1)
	{}
};

/**
 * Category budget configuration.
 *
 * Limits how many intents from a category can be attempted per tick.
 */
struct CategoryBudget
{
	IntentCategory category;     // Category this budget applies to
	int maxAttemptsPerTick;      // Maximum attempts allowed per tick
	int currentAttempts;         // Attempts consumed this tick

	CategoryBudget()
		: category(IntentCategory::PRODUCTION)
		, maxAttemptsPerTick(1)
		, currentAttempts(0)
	{}

	CategoryBudget(IntentCategory cat, int maxAttempts)
		: category(cat)
		, maxAttemptsPerTick(maxAttempts)
		, currentAttempts(0)
	{}
};

/**
 * Scheduler configuration.
 *
 * Defines category budgets and scheduling policy.
 */
struct SchedulerConfig
{
	std::vector<CategoryBudget> categoryBudgets;

	SchedulerConfig()
	{
		// Default budgets:
		// - Tech: 1 science + 1 upgrade per tick (science and upgrades are separate categories)
		// - Production: 3 attempts per tick (allows fallback when queue full)
		// - Defense: 1 attempt per tick (subset of production, higher priority)
		// - Defense Response: 1 attempt per tick (Phase 7: immediate unit mobilization to threatened zones)
		// - Economy: 1 attempt per tick (income recovery builds)
		categoryBudgets.push_back(CategoryBudget(IntentCategory::TECH_SCIENCE, 1));
		categoryBudgets.push_back(CategoryBudget(IntentCategory::TECH_UPGRADE, 1));
		categoryBudgets.push_back(CategoryBudget(IntentCategory::PRODUCTION, 3));
		categoryBudgets.push_back(CategoryBudget(IntentCategory::DEFENSE, 1));
		categoryBudgets.push_back(CategoryBudget(IntentCategory::DEFENSE_RESPONSE, 1));
		categoryBudgets.push_back(CategoryBudget(IntentCategory::ECONOMY, 1));
	}
};

struct AIControlAdapterAutonomyTickSchedule
{
	bool macroDue;
	bool productionDue;
	bool techDue;
	bool guardDue;
	bool anyDue;
	bool guardOnly;

	AIControlAdapterAutonomyTickSchedule()
		: macroDue(false)
		, productionDue(false)
		, techDue(false)
		, guardDue(false)
		, anyDue(false)
		, guardOnly(false)
	{}
};

/**
 * Intent scheduler for autonomous behavior arbitration.
 *
 * Manages intent queue, priority arbitration, and category budget enforcement.
 * Extracted from AIControlAdapter main loop to provide clear action arbitration.
 */
class AIControlAdapterScheduler
{
public:
	AIControlAdapterScheduler();
	explicit AIControlAdapterScheduler(const SchedulerConfig& config);

	/**
	 * Submit an intent for scheduling.
	 *
	 * Intents are queued until ProcessIntents() is called.
	 * Managers should submit all their intents before calling ProcessIntents().
	 *
	 * @param intent Intent to schedule
	 */
	void SubmitIntent(const Intent& intent);

	/**
	 * Process all submitted intents for the current tick.
	 *
	 * Sorts intents by priority, enforces category budgets, and attempts execution.
	 * Returns results for all intents (attempted, skipped, etc.).
	 *
	 * Processing order:
	 * 1. Sort intents by priority (CRITICAL -> HIGH -> NORMAL -> LOW)
	 * 2. For each intent in priority order:
	 *    a. Check category budget
	 *    b. If budget available, attempt execution via executeFunc
	 *    c. Consume budget attempt regardless of success/failure
	 *    d. Record result with execution state and reason
	 * 3. Mark remaining intents as skipped if budget exhausted
	 * 4. Clear intent queue and reset budgets for next tick
	 *
	 * @param currentTick Current game tick for result telemetry
	 * @return Vector of intent results for all submitted intents
	 */
	std::vector<IntentResult> ProcessIntents(unsigned int currentTick);

	/**
	 * Get current category budget state (for debugging/telemetry).
	 *
	 * @return Vector of category budgets with current attempt counts
	 */
	std::vector<CategoryBudget> GetCategoryBudgets() const;

	/**
	 * Reset scheduler state (called when starting new game).
	 *
	 * Clears intent queue and resets category budgets.
	 */
	void Reset();

	static AIControlAdapterAutonomyTickSchedule EvaluateAutonomyTickSchedule(
		unsigned int now,
		unsigned int nextMacroTick,
		unsigned int nextProductionTick,
		unsigned int nextTechTick,
		unsigned int nextGuardTick);

private:
	SchedulerConfig m_config;
	std::vector<Intent> m_pendingIntents;

	/**
	 * Find budget for a category.
	 *
	 * @param category Category to find
	 * @return Pointer to budget entry, or nullptr if not found
	 */
	CategoryBudget* FindBudget(IntentCategory category);

	/**
	 * Check if category budget has capacity for another attempt.
	 *
	 * @param category Category to check
	 * @return True if budget allows another attempt
	 */
	bool HasBudget(IntentCategory category);

	/**
	 * Consume one attempt from category budget.
	 *
	 * @param category Category to consume budget from
	 */
	void ConsumeBudget(IntentCategory category);

	/**
	 * Reset all category budgets to zero attempts.
	 */
	void ResetBudgets();

	/**
	 * Sort intents by priority (higher priority first).
	 */
	static bool ComparePriority(const Intent& a, const Intent& b);
};
