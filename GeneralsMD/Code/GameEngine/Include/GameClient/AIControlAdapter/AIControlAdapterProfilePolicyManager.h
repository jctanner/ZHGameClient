/**
 * AIControlAdapterProfilePolicyManager.h
 *
 * Profile-derived autonomy policy configuration.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <string>

struct AIControlAdapterProfilePolicyOverrides
{
	bool hasUrgentZoneGapThreshold = false;
	int urgentZoneGapThreshold = 0;
	bool hasMaxConcurrentExpansionStashes = false;
	int maxConcurrentExpansionStashes = 0;
	bool hasAllowExpansionBeforeFullRemoteFollowup = false;
	bool allowExpansionBeforeFullRemoteFollowup = false;
	bool hasExpansionHighCashFloatThreshold = false;
	unsigned int expansionHighCashFloatThreshold = 0u;
};

struct AIControlAdapterProfilePolicyRequest
{
	std::string profile;
	float economyBias = 0.5f;
	float aggressionBias = 0.5f;
	float defenseBias = 0.5f;
	float expansionBias = 0.5f;
	float sprawlMultiplier = 1.0f;
	AIControlAdapterProfilePolicyOverrides overrides;
};

class AIControlAdapterProfilePolicyManager
{
public:
	AIControlAdapterProfilePolicyConfig Resolve(const AIControlAdapterProfilePolicyRequest& request) const;
	int ResolveStrategicSpendDesiredZoneCount(
		const AIControlAdapterProfilePolicyConfig& config,
		int nonSprawlFallback) const;
	unsigned int ResolveGuardCadenceMs(const AIControlAdapterProfilePolicyConfig& config) const;
	unsigned int ResolveReserveCashWithFloor(
		const AIControlAdapterProfilePolicyConfig& config,
		unsigned int minimumReserveCash) const;
	int ResolveCombatArmyCapBase(const AIControlAdapterProfilePolicyConfig& config) const;
	bool UsesTechRetryPolicy(const AIControlAdapterProfilePolicyConfig& config) const;
	bool AllowsRadarVanAutomation(const AIControlAdapterProfilePolicyConfig& config) const;
	int ResolveRadarVanMinCount(const AIControlAdapterProfilePolicyConfig& config) const;
	bool WantsBaselineTwoMarkets(const AIControlAdapterProfilePolicyConfig& config) const;
	bool WantsAcceleratedOpeningSupply(const AIControlAdapterProfilePolicyConfig& config, float expansionBias) const;
};
