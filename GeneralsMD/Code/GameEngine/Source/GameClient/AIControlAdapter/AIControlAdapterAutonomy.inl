	/**
	 * AIControlAdapterAutonomy.inl
	 *
	 * Autonomy mode state management for AI Control Adapter.
	 *
	 * The autonomy system allows the adapter to play the game autonomously without external
	 * commands. This is useful for:
	 * - Hybrid AI: Adapter handles economy/macro, external agent handles combat strategy
	 * - Testing: Autonomous mode provides baseline AI behavior for comparison
	 * - Training: RL agents can learn from/against autonomous behavior
	 * - Demonstration: Shows what the adapter can do without external control
	 *
	 * AUTONOMY ARCHITECTURE
	 * ====================
	 *
	 * The autonomy system operates in layers:
	 *
	 * 1. Mode Control:
	 *    - "manual": Autonomy disabled, external agent sends all commands
	 *    - "economy": Autonomous economy only (workers, buildings)
	 *    - "combat": Autonomous combat only (unit production, attacks)
	 *    - "full": Fully autonomous (economy + combat)
	 *    - "builtin_passthrough": Special mode for testing
	 *
	 * 2. Profile Selection:
	 *    Profiles configure automation rules with different playstyles:
	 *    - "aggressive": Early aggression, minimal economy, fast attacks
	 *    - "balanced": Mix of economy and military (default)
	 *    - "defensive": Strong economy, delayed aggression, larger armies
	 *    - "economic": Maximum economy focus, late-game scaling
	 *    - "tech": Tech upgrades priority, moderate aggression
	 *    - "sprawl": Multi-base expansion, wide economic footprint
	 *    - "sprawl_balanced": Balanced variant of sprawl
	 *
	 * 3. Bias Parameters:
	 *    Fine-tune behavior within a profile (0.0 to 1.0 scale):
	 *    - economyBias: Worker count, income buildings priority
	 *    - aggressionBias: Attack frequency, army size threshold
	 *    - defenseBias: Defensive structures, army retention
	 *    - expansionBias: Expansion radius, remote base likelihood
	 *
	 * 4. Automation Rules:
	 *    Autonomy mode configures automation rules that execute each frame:
	 *    - Worker rule: Auto-build workers, maintain idle worker count
	 *    - Stash worker rule: Assign workers to Supply Stashes
	 *    - Attack rule: Auto-attack when army size threshold reached
	 *    - Capture rule: Auto-capture neutral tech buildings
	 *    - Radar van rule: Maintain radar van count
	 *
	 * 5. Macro/Production Systems:
	 *    Higher-level decision making (evaluated periodically):
	 *    - Macro system: Build economy/production buildings, expand bases
	 *    - Production system: Queue combat units based on composition policy
	 *    - Tech system: Research science, purchase upgrades (sequential plans)
	 *    - Guard system: Position idle units at defensive locations
	 *
	 * CONFIGURATION API
	 * =================
	 *
	 * External agents configure autonomy via commands:
	 *
	 * Autonomy.SetMode:
	 * {"type":"SessionCommand", "cmd":"Autonomy.SetMode", "args":{"mode":"economy","enabled":true}}
	 *
	 * Autonomy.Configure:
	 * {"type":"SessionCommand", "cmd":"Autonomy.Configure", "args":{
	 *   "profile":"sprawl_balanced",
	 *   "economy_bias":0.8,
	 *   "aggression_bias":0.3,
	 *   "capture_tech":true,
	 *   "target_player_index":1
	 * }}
	 *
	 * Autonomy.Pause / Autonomy.Resume:
	 * {"type":"SessionCommand", "cmd":"Autonomy.Pause"}
	 *
	 * Autonomy.Status:
	 * {"type":"SessionCommand", "cmd":"Autonomy.Status"}
	 * Returns: {"ok":true, "result":{"mode":"economy", "profile":"balanced", "paused":false, ...}}
	 *
	 * Autonomy.Telemetry:
	 * {"type":"SessionCommand", "cmd":"Autonomy.Telemetry"}
	 * Returns: Decision history, zone info, recent events
	 *
	 * HYBRID CONTROL
	 * ==============
	 *
	 * Common pattern: Autonomy handles economy, external agent handles combat:
	 * 1. Set mode to "economy" (adapter builds workers, economy buildings)
	 * 2. External agent sends Game.QueueUnit, Game.AttackMove commands
	 * 3. Adapter maintains economic foundation, agent controls military
	 *
	 * This division of labor allows:
	 * - RL agents to focus on learning combat strategy
	 * - Consistent economic baseline across training runs
	 * - Reduced action space for agent (no economy micromanagement)
	 *
	 * TELEMETRY & OBSERVABILITY
	 * =========================
	 *
	 * The autonomy system provides telemetry for analysis:
	 * - lastDecisionCategory/Command/Reason: Most recent autonomy decision
	 * - telemetryEvents: Recent autonomy events (builds, attacks, state changes)
	 * - telemetryZones: Zone management state (expansions, front lines)
	 * - nextMacroTick/nextProductionTick: Timing of next evaluations
	 *
	 * See also:
	 * - AIControlAdapterPolicy.h for decision logic
	 * - AIControlAdapterAutomation.inl for automation rule execution
	 * - AIControlAdapter.cpp applyAutonomyRules() for profile configuration
	 */

	/**
	 * Autonomy mode state container.
	 *
	 * Manages configuration and runtime state for autonomous behavior.
	 */
	class AdapterAutonomyState
	{
	public:
		AdapterAutonomyState()
		{
			reset();
		}

		/**
		 * Reset autonomy state to default configuration.
		 *
		 * Called when:
		 * - Adapter initializes
		 * - Game resets (new match, return to menu)
		 * - Autonomy.Reset command received
		 */
		void reset()
		{
			state.mode = "manual";
			state.profile = "standard";
			state.paused = false;
			state.hasAttackAutomationEnabledOverride = false;
			state.attackAutomationEnabled = false;
			state.captureTech = false;
			state.allowSuperweapons = false;
			state.hasExplicitPlayerIndex = false;
			state.playerIndex = -1;
			state.hasExplicitTargetPlayerIndex = false;
			state.targetPlayerIndex = -1;
			state.economyBias = 0.5f;
			state.aggressionBias = 0.5f;
			state.defenseBias = 0.5f;
			state.expansionBias = 0.5f;
			state.sprawlMultiplier = 1.0f;
			state.zoneRadius = 300.0f;
			state.lastAppliedTick = 0u;
			state.nextMacroTick = 0u;
			state.nextProductionTick = 0u;
			state.nextTechTick = 0u;
			state.nextGuardTick = 0u;
			state.nextSupplyBuildTick = 0u;
			state.nextBarracksBuildTick = 0u;
			state.nextArmsBuildTick = 0u;
			state.nextPalaceBuildTick = 0u;
			state.nextMarketBuildTick = 0u;
			state.nextTunnelBuildTick = 0u;
			state.nextStingerBuildTick = 0u;
			state.nextZoneIndex = 0u;
			state.hasLastZone = false;
			state.lastZoneAnchorId = 0u;
			state.lastZoneIsMainBase = false;
			state.lastZoneCenterX = 0.0f;
			state.lastZoneCenterY = 0.0f;
			state.lastDecisionCategory.clear();
			state.lastDecisionCommand.clear();
			state.lastDecisionReason.clear();
			state.telemetryZones = nlohmann::json::array();
			state.telemetryEvents = nlohmann::json::array();
		}

		/**
		 * Check if autonomy mode is currently active.
		 *
		 * Autonomy is active when:
		 * - Mode is not "manual" (economy, combat, or full)
		 * - Not paused (Autonomy.Pause not called)
		 *
		 * When active:
		 * - Automation rules execute each frame
		 * - Macro/production systems evaluate periodically
		 * - External commands still work (manual commands override autonomy)
		 *
		 * When inactive:
		 * - All automation rules disabled
		 * - External agent must send all commands
		 *
		 * @return True if autonomy is active, false if manual mode or paused
		 */
		bool isActive() const
		{
			return !state.paused && state.mode != "manual";
		}

		AutonomyState state;  ///< Current autonomy configuration and runtime state
	};
	};
