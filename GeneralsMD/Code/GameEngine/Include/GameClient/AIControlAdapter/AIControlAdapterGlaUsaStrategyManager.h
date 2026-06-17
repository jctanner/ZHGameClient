/**
 * AIControlAdapterGlaUsaStrategyManager.h
 *
 * Phase 14 GLA-vs-USA strategy policy.
 */

#pragma once

struct AIControlAdapterGlaUsaStrategyInput
{
	bool enemyUsaDetected = false;
	int enemyUsaWmdTargets = 0;
	int enemyArmorThreats = 0;
	int enemyAirThreats = 0;
	int enemyMixedThreats = 0;
	int criticalZoneCount = 0;
	bool mainBaseCritical = false;
	bool producerSpineBroken = false;
	bool collapseImminent = false;

	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	int completedPalaces = 0;
	int readyBarracks = 0;
	int readyArmsDealers = 0;
	int tunnels = 0;
	int stingers = 0;
	int exposedExpansionZones = 0;

	int quads = 0;
	int queuedQuads = 0;
	int scorpions = 0;
	int queuedScorpions = 0;
	int rocketBuggies = 0;
	int armyCount = 0;
	int activeDefenseTasks = 0;
	int activeAttackTasks = 0;

	int workers = 0;
	int technicals = 0;
	int queuedTechnicals = 0;
	int remoteBuildGap = 0;
	float farthestRemoteBuildDistance = 0.0f;
};

struct AIControlAdapterGlaUsaStrategyResult
{
	bool active = false;
	const char* pressure = "none";
	const char* tunnelRole = "standard_defense";
	const char* reason = "no_usa_enemy";

	int scorpionFloor = 0;
	int quadFloor = 0;
	int reservedStrikeGroup = 0;
	bool tunnelMissilesSatisfyArmor = true;

	bool preferScorpionProduction = false;
	bool preferQuadProduction = false;
	bool preserveAttackGroup = false;
	bool prioritizeProducerRecovery = false;

	bool camouflageDesired = false;
	bool camouflageSpendAllowed = false;
	const char* camouflageReason = "no_usa_enemy";

	bool workerMobilityDesired = false;
	const char* workerMobilityMode = "walk";
	int desiredShuttleTechnicals = 0;
	int protectedShuttleTechnicals = 0;
	bool shuttleTechnicalProductionNeeded = false;
	const char* shuttleReservationReason = "not_needed";
	const char* workerMobilityReason = "local_builds";

	bool wmdConstructionDiagnosticNeeded = false;
	const char* wmdConstructionReason = "no_enemy_wmd";
};

class AIControlAdapterGlaUsaStrategyManager
{
public:
	AIControlAdapterGlaUsaStrategyResult Evaluate(const AIControlAdapterGlaUsaStrategyInput& input) const;
};
