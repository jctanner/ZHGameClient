/**
 * AIControlAdapterSurvivalPolicyManager.h
 *
 * Phase 13 survival-state classification for adapter orchestration.
 */

#pragma once

struct AIControlAdapterSurvivalPolicyInput
{
	int criticalZoneCount = 0;
	int defenseReserveDeficits = 0;
	int activeCombatTasks = 0;
	int readyScudStorms = 0;
	int enemyWmdTargets = 0;
	int readyBarracks = 0;
	int readyArmsDealers = 0;
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
};

struct AIControlAdapterSurvivalPolicyResult
{
	const char* state = "stable";
	const char* priority = "normal_macro";
	const char* reason = "stable";
	bool collapseImminent = false;
	bool wmdCrisis = false;
	bool producerSpineBroken = false;
	bool blockExposedWmdFoundations = false;
};

class AIControlAdapterSurvivalPolicyManager
{
public:
	AIControlAdapterSurvivalPolicyResult Evaluate(const AIControlAdapterSurvivalPolicyInput& input) const;
};
