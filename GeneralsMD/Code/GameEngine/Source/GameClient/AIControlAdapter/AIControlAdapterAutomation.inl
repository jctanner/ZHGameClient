	/**
	 * AIControlAdapterAutomation.inl
	 *
	 * Automation rule state management for AI Control Adapter.
	 *
	 * Automation rules are frame-by-frame behaviors that execute automatically when enabled.
	 * They differ from autonomy in scope:
	 *
	 * AUTOMATION RULES (this file):
	 * - Low-level, repetitive actions (build workers, auto-attack, assign workers to stashes)
	 * - Execute every frame when enabled (with cooldowns to prevent spam)
	 * - Configured via Automation.Configure* commands OR by autonomy system
	 * - Work independently of each other (rule composition)
	 * - Can be used with autonomy mode OR manually configured by external agents
	 *
	 * AUTONOMY MODE (AIControlAdapterAutonomy.inl):
	 * - High-level strategic decision making (what to build, when to attack, where to expand)
	 * - Evaluates periodically (not every frame)
	 * - Configures automation rules based on profile (aggressive, defensive, etc.)
	 * - Also handles macro/production/tech systems beyond automation rules
	 *
	 * RULE TYPES
	 * ==========
	 *
	 * WorkerAutomationRule (workerRule):
	 * - Maintains minimum idle worker count
	 * - Auto-queues workers from Command Center when count drops below threshold
	 * - Parameters: minIdleWorkers, queueCount, cooldownMs
	 * - Use case: Ensure economy always has workers available
	 *
	 * StashWorkerAutomationRule (stashWorkerRule):
	 * - Assigns workers to Supply Stashes (income buildings)
	 * - Maintains targetWorkersPerStash per Supply Stash
	 * - Parameters: targetWorkersPerStash, cooldownMs
	 * - Use case: Automate worker assignment to maximize income
	 *
	 * AttackAutomationRule (attackRule):
	 * - Auto-attack enemy when army size reaches threshold
	 * - Groups combat units and attack-moves to enemy base
	 * - Parameters: minUnits, groupSize, distance, cooldownMs
	 * - Use case: Automatic aggression without manual attack commands
	 *
	 * CaptureAutomationRule (captureRule):
	 * - Auto-capture neutral tech buildings with workers
	 * - Finds nearby tech buildings and sends workers to capture
	 * - Parameters: preferIdle, maxConcurrent, cooldownMs
	 * - Use case: Secure tech buildings for upgrades/special units
	 *
	 * RadarVanAutomationRule (radarVanRule):
	 * - Maintains minimum radar van count
	 * - Auto-queues radar vans when count below threshold
	 * - Parameters: minCount, cooldownMs
	 * - Use case: Ensure stealth detection coverage
	 *
	 * CONFIGURATION
	 * =============
	 *
	 * Rules can be configured two ways:
	 *
	 * 1. Manual (external agent):
	 *    {"type":"SessionCommand", "cmd":"Automation.ConfigureWorkerRule", "args":{
	 *      "enabled":true, "min_idle_workers":3, "queue_count":2, "cooldown_ms":2000
	 *    }}
	 *
	 * 2. Automatic (autonomy mode):
	 *    applyAutonomyRules() configures all rules based on profile
	 *    Example: "aggressive" profile sets minUnits=24, "defensive" sets minUnits=60
	 *
	 * When autonomy mode activates, it overwrites manual rule configuration!
	 * Use "manual" mode if you want full control over automation rules.
	 *
	 * EXECUTION MODEL
	 * ===============
	 *
	 * Each frame (AIControlAdapterUpdate):
	 * 1. evaluateAutomationRules() called
	 * 2. For each enabled rule:
	 *    - Check if cooldown expired (tick >= nextAllowedTick)
	 *    - Evaluate rule condition (e.g., idle worker count < threshold)
	 *    - Execute rule action (e.g., queue worker production)
	 *    - Set next cooldown (nextAllowedTick = now + cooldownMs)
	 * 3. Rules execute independently (one rule doesn't block another)
	 *
	 * Cooldowns prevent spam and allow rules to coexist without conflicts.
	 *
	 * See also:
	 * - AIControlAdapter.cpp evaluateAutomationRules() for rule execution logic
	 * - AIControlAdapterAutonomy.inl for autonomy mode that configures these rules
	 * - AIControlAdapterWorkers.inl for worker management utilities
	 */

	/**
	 * Automation rule state container.
	 *
	 * Holds configuration for all automation rules. Rules are enabled/disabled
	 * and configured either manually (via Automation.Configure* commands) or
	 * automatically (by autonomy mode based on profile).
	 */
	class AdapterAutomationState
	{
	public:
		AdapterAutomationState()
		{
			reset();
		}

		/**
		 * Reset all automation rules to default (disabled) state.
		 *
		 * Called when:
		 * - Adapter initializes
		 * - Game resets (new match, return to menu)
		 */
		void reset()
		{
			workerRule.enabled = false;
			workerRule.hasExplicitPlayerIndex = false;
			workerRule.hasExplicitProducerKind = false;
			workerRule.playerIndex = -1;
			workerRule.minIdleWorkers = 0;
			workerRule.queueCount = 1;
			workerRule.producerKind.clear();
			workerRule.cooldownMs = 3000u;
			workerRule.nextAllowedTick = 0u;

			attackRule.enabled = false;
			attackRule.hasExplicitPlayerIndex = false;
			attackRule.playerIndex = -1;
			attackRule.minUnits = 40;
			attackRule.groupSize = 30;
			attackRule.distance = 3000.0f;
			attackRule.cooldownMs = 15000u;
			attackRule.nextAllowedTick = 0u;

			captureRule.enabled = false;
			captureRule.hasExplicitPlayerIndex = false;
			captureRule.preferIdle = true;
			captureRule.playerIndex = -1;
			captureRule.maxConcurrent = 4;
			captureRule.cooldownMs = 4000u;
			captureRule.nextAllowedTick = 0u;
			captureRule.pendingTargetsUntilTick.clear();
			captureRule.pendingSourcesUntilTick.clear();

			radarVanRule.enabled = false;
			radarVanRule.hasExplicitPlayerIndex = false;
			radarVanRule.playerIndex = -1;
			radarVanRule.minCount = 1;
			radarVanRule.cooldownMs = 12000u;
			radarVanRule.nextAllowedTick = 0u;

			stashWorkerRule.enabled = false;
			stashWorkerRule.hasExplicitPlayerIndex = false;
			stashWorkerRule.playerIndex = -1;
			stashWorkerRule.targetWorkersPerStash = 9;
			stashWorkerRule.cooldownMs = 4000u;
			stashWorkerRule.nextAllowedTick = 0u;
			stashWorkerRule.servicedStashIds.clear();
		}

		/**
		 * Disable all automation rules.
		 *
		 * Called when:
		 * - Autonomy mode deactivates (switch to manual mode)
		 * - External agent requests full manual control
		 *
		 * Clears pending state (pending captures, serviced stashes) to prevent
		 * stale data from affecting future rule executions.
		 */
		void clearAllRules()
		{
			workerRule.enabled = false;
			attackRule.enabled = false;
			captureRule.enabled = false;
			captureRule.pendingTargetsUntilTick.clear();
			captureRule.pendingSourcesUntilTick.clear();
			radarVanRule.enabled = false;
			stashWorkerRule.enabled = false;
			stashWorkerRule.servicedStashIds.clear();
		}

		WorkerAutomationRule workerRule;        ///< Auto-build workers to maintain idle count
		AttackAutomationRule attackRule;        ///< Auto-attack when army size threshold reached
		CaptureAutomationRule captureRule;      ///< Auto-capture neutral tech buildings
		RadarVanAutomationRule radarVanRule;    ///< Maintain radar van count
		StashWorkerAutomationRule stashWorkerRule;  ///< Assign workers to Supply Stashes
	};
