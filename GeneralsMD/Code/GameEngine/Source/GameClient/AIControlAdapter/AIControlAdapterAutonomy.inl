	class AdapterAutonomyState
	{
	public:
		AdapterAutonomyState()
		{
			reset();
		}

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

		bool isActive() const
		{
			return !state.paused && state.mode != "manual";
		}

		AutonomyState state;
	};
