	class AdapterAutomationState
	{
	public:
		AdapterAutomationState()
		{
			reset();
		}

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
			captureRule.maxConcurrent = 3;
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

		WorkerAutomationRule workerRule;
		AttackAutomationRule attackRule;
		CaptureAutomationRule captureRule;
		RadarVanAutomationRule radarVanRule;
		StashWorkerAutomationRule stashWorkerRule;
	};
