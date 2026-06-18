/**
 * AIControlAdapterMacroBuildDispatcher.h
 *
 * Converts selected macro build intents into typed dispatch requests and
 * normalized execution results. The adapter owns the actual game callbacks.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildManager.h"

#include <string>

enum class AIControlAdapterMacroBuildDispatchKind
{
	None,
	MacroBuild,
	SupplyExpansion,
	SpecificZone
};

struct AIControlAdapterMacroBuildDispatchRequest
{
	bool shouldDispatch = false;
	int intentIndex = -1;
	AIControlAdapterMacroBuildDispatchKind kind = AIControlAdapterMacroBuildDispatchKind::None;
	std::string category = "none";
	std::string command = "none";
	std::string reason = "no_valid_intent";
	std::string taskName = "auto_macro_zone";
	int priority = 0;
	bool preferZone = false;
	int zoneIndex = -1;
};

struct AIControlAdapterMacroBuildDispatchResult
{
	bool attempted = false;
	bool issued = false;
	std::string category = "none";
	std::string command = "none";
	std::string reason = "not_attempted";
	int priority = 0;
};

struct AIControlAdapterMacroBuildCooldownSlots
{
	unsigned long* supply = nullptr;
	unsigned long* barracks = nullptr;
	unsigned long* armsDealer = nullptr;
	unsigned long* palace = nullptr;
	unsigned long* blackMarket = nullptr;
	unsigned long* tunnel = nullptr;
	unsigned long* stinger = nullptr;
};

struct AIControlAdapterMacroBuildCommandAlias
{
	std::string command;
	std::string buildingTemplate;
	bool hasBuildingTemplate = false;
};

class AIControlAdapterMacroBuildDispatcher
{
public:
	AIControlAdapterMacroBuildDispatchRequest BuildDispatch(
		const AIControlAdapterMacroBuildPlan& plan) const;

	AIControlAdapterMacroBuildDispatchResult CompleteDispatch(
		const AIControlAdapterMacroBuildDispatchRequest& request,
		bool issued,
		const std::string& executionReason) const;

	static unsigned long* FindBuildCooldownTick(
		AIControlAdapterMacroBuildCooldownSlots& slots,
		const char* commandName);

	static const unsigned long* FindBuildCooldownTick(
		const AIControlAdapterMacroBuildCooldownSlots& slots,
		const char* commandName);

	static bool IsBuildCooldownReady(
		const AIControlAdapterMacroBuildCooldownSlots& slots,
		const char* commandName,
		unsigned long now);

	static bool IsBuildAttemptReady(
		const AIControlAdapterMacroBuildCooldownSlots& slots,
		const char* commandName,
		int inProgressCount,
		unsigned long now);

	static void RecordBuildAttempt(
		AIControlAdapterMacroBuildCooldownSlots& slots,
		const char* commandName,
		bool success,
		const std::string& reason,
		unsigned long now);

	static AIControlAdapterMacroBuildCommandAlias ResolveCommandAlias(
		const char* commandName);
};
