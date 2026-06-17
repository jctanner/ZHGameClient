/**
 * AIControlAdapterStrategicFoundationSurvivalManager.h
 *
 * Phase 13 strategic foundation survival/rebuild policy.
 */

#pragma once

#include <string>
#include <vector>

struct AIControlAdapterStrategicFoundationFact
{
	unsigned int foundationId = 0u;
	std::string templateName;
	float x = 0.0f;
	float y = 0.0f;
	float lastHealth = -1.0f;
	unsigned int nowTick = 0u;
	unsigned int firstSeenTick = 0u;
	unsigned int lastSeenTick = 0u;
	unsigned int lastProgressTick = 0u;
	int recoveryAttempts = 0;
	bool stopIssued = false;
	std::string reason;
};

struct AIControlAdapterStrategicFoundationClassification
{
	const char* state = "unknown";
	const char* reason = "not_evaluated";
	bool failed = false;
	bool rebuildBlocked = false;
};

struct AIControlAdapterScudStormRebuildBlockInput
{
	float x = 0.0f;
	float y = 0.0f;
	unsigned int nowTick = 0u;
	unsigned int recentFailureMs = 300000u;
	float radius = 650.0f;
	bool collapseImminent = false;
	std::vector<AIControlAdapterStrategicFoundationFact> foundations;
};

struct AIControlAdapterScudStormRebuildBlockResult
{
	bool blocked = false;
	unsigned int foundationId = 0u;
	const char* reason = "allowed";
};

class AIControlAdapterStrategicFoundationSurvivalManager
{
public:
	AIControlAdapterStrategicFoundationClassification Classify(
		const AIControlAdapterStrategicFoundationFact& fact) const;
	AIControlAdapterScudStormRebuildBlockResult ShouldBlockScudStormRebuild(
		const AIControlAdapterScudStormRebuildBlockInput& input) const;
};
