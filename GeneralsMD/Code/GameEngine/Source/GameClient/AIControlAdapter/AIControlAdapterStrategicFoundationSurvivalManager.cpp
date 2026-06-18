#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterStrategicFoundationSurvivalManager.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool containsScudStorm(const std::string& value)
	{
		return value.find("ScudStorm") != std::string::npos || value.find("scudstorm") != std::string::npos;
	}

	bool isSubstantialFoundationProgress(const AIControlAdapterStrategicFoundationFact& fact)
	{
		if (fact.maxHealth > 0.0f && fact.lastHealth >= fact.maxHealth * 0.50f)
		{
			return true;
		}
		return fact.lastHealth >= 2000.0f;
	}
}

AIControlAdapterStrategicFoundationClassification AIControlAdapterStrategicFoundationSurvivalManager::Classify(
	const AIControlAdapterStrategicFoundationFact& fact) const
{
	AIControlAdapterStrategicFoundationClassification result;
	result.reason = fact.reason.empty() ? "healthy_in_progress" : fact.reason.c_str();
	result.tombstoned = fact.tombstoned;

	if (fact.reason == "destroyed")
	{
		result.state = "destroyed";
		result.failed = true;
		result.rebuildBlocked = containsScudStorm(fact.templateName);
		return result;
	}
	if (fact.stopIssued || fact.reason == "stopped_stale_no_progress" || fact.reason == "stopped_no_builder_timeout" || fact.reason == "stopped_worker_dead")
	{
		if (containsScudStorm(fact.templateName) && fact.activeWmdThreat && isSubstantialFoundationProgress(fact))
		{
			result.state = "recoverable";
			result.reason = fact.tombstoned ? "tombstoned_high_progress_wmd_recovery" : "high_progress_wmd_recovery";
			result.failed = false;
			result.rebuildBlocked = false;
			result.recoverable = true;
			return result;
		}
		result.state = "stopped";
		result.failed = true;
		result.rebuildBlocked = containsScudStorm(fact.templateName);
		return result;
	}
	if (fact.reason == "no_active_builder" || fact.reason == "stalled_no_builder" || fact.reason == "worker_dead")
	{
		if (containsScudStorm(fact.templateName) && fact.activeWmdThreat && isSubstantialFoundationProgress(fact))
		{
			result.state = "recoverable";
			result.reason = "no_builder_high_progress_wmd_recovery";
			result.failed = false;
			result.rebuildBlocked = false;
			result.recoverable = true;
			return result;
		}
		result.state = "no_builder";
		result.failed = fact.recoveryAttempts >= 2;
		return result;
	}
	if (fact.lastProgressTick != 0u && fact.nowTick >= fact.lastProgressTick + 60000u)
	{
		if (containsScudStorm(fact.templateName) && fact.activeWmdThreat && isSubstantialFoundationProgress(fact))
		{
			result.state = "recoverable";
			result.reason = "stale_high_progress_wmd_recovery";
			result.failed = false;
			result.rebuildBlocked = false;
			result.recoverable = true;
			return result;
		}
		result.state = "stale";
		result.failed = fact.recoveryAttempts >= 1;
		return result;
	}

	result.state = "healthy";
	result.reason = "healthy_in_progress";
	return result;
}

AIControlAdapterScudStormRebuildBlockResult AIControlAdapterStrategicFoundationSurvivalManager::ShouldBlockScudStormRebuild(
	const AIControlAdapterScudStormRebuildBlockInput& input) const
{
	AIControlAdapterScudStormRebuildBlockResult result;
	const float radiusSq = std::max(0.0f, input.radius) * std::max(0.0f, input.radius);

	for (const AIControlAdapterStrategicFoundationFact& fact : input.foundations)
	{
		if (!containsScudStorm(fact.templateName))
		{
			continue;
		}
		AIControlAdapterStrategicFoundationFact classifiedFact = fact;
		classifiedFact.nowTick = input.nowTick;
		const AIControlAdapterStrategicFoundationClassification classification = Classify(classifiedFact);
		if (!classification.failed && !classification.rebuildBlocked)
		{
			continue;
		}
		const unsigned int lastSeenTick = fact.lastSeenTick != 0u ? fact.lastSeenTick : fact.firstSeenTick;
		if (lastSeenTick == 0u || input.nowTick > lastSeenTick + input.recentFailureMs)
		{
			continue;
		}
		const float dx = fact.x - input.x;
		const float dy = fact.y - input.y;
		if ((dx * dx) + (dy * dy) > radiusSq)
		{
			continue;
		}

		result.blocked = true;
		result.foundationId = fact.foundationId;
		result.reason = input.collapseImminent ? "collapse_exposed_recent_failure" : "recent_failed_scud_foundation";
		return result;
	}

	return result;
}
