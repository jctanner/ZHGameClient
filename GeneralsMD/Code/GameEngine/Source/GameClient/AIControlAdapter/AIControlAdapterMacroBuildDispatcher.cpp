#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildDispatcher.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

namespace
{
	std::string safeString(const char* value, const char* fallback)
	{
		return value != nullptr && value[0] != '\0' ? std::string(value) : std::string(fallback);
	}

	AIControlAdapterMacroBuildDispatchKind toDispatchKind(AIControlAdapterMacroBuildExecutor executor)
	{
		switch (executor)
		{
		case AIControlAdapterMacroBuildExecutor::SupplyExpansion:
			return AIControlAdapterMacroBuildDispatchKind::SupplyExpansion;
		case AIControlAdapterMacroBuildExecutor::SpecificZone:
			return AIControlAdapterMacroBuildDispatchKind::SpecificZone;
		case AIControlAdapterMacroBuildExecutor::MacroBuild:
		default:
			return AIControlAdapterMacroBuildDispatchKind::MacroBuild;
		}
	}

	bool hasTickElapsed(unsigned long deadlineTick, unsigned long now)
	{
		return deadlineTick == 0u || static_cast<long>(now - deadlineTick) >= 0;
	}
}

AIControlAdapterMacroBuildDispatchRequest AIControlAdapterMacroBuildDispatcher::BuildDispatch(
	const AIControlAdapterMacroBuildPlan& plan) const
{
	AIControlAdapterMacroBuildDispatchRequest request;
	request.intentIndex = plan.choice.index;
	request.category = safeString(plan.choice.category, "none");
	request.command = safeString(plan.choice.command, "none");
	request.reason = safeString(plan.choice.reason, "no_valid_intent");
	request.priority = plan.choice.priority;

	if (plan.choice.index < 0 || plan.choice.index >= static_cast<int>(plan.intents.size()))
	{
		return request;
	}

	const AIControlAdapterMacroBuildIntent& selected = plan.intents[static_cast<std::size_t>(plan.choice.index)];
	request.shouldDispatch = selected.option.command != nullptr && selected.option.command[0] != '\0';
	request.kind = toDispatchKind(selected.executor);
	request.category = safeString(selected.option.category, request.category.c_str());
	request.command = safeString(selected.option.command, request.command.c_str());
	request.reason = safeString(selected.option.reason, request.reason.c_str());
	request.priority = selected.option.priority;
	request.preferZone = selected.preferZone;
	request.zoneIndex = selected.zoneIndex;
	request.taskName = safeString(selected.taskName, "auto_macro_zone");
	return request;
}

AIControlAdapterMacroBuildDispatchResult AIControlAdapterMacroBuildDispatcher::CompleteDispatch(
	const AIControlAdapterMacroBuildDispatchRequest& request,
	bool issued,
	const std::string& executionReason) const
{
	AIControlAdapterMacroBuildDispatchResult result;
	result.attempted = request.shouldDispatch;
	result.issued = issued;
	result.category = request.category;
	result.command = request.command;
	result.priority = request.priority;
	result.reason = executionReason.empty() ? request.reason : executionReason;
	if (result.reason.empty())
	{
		result.reason = issued ? "ok" : "selected";
	}
	return result;
}

unsigned long* AIControlAdapterMacroBuildDispatcher::FindBuildCooldownTick(
	AIControlAdapterMacroBuildCooldownSlots& slots,
	const char* commandName)
{
	const std::string command = commandName != nullptr ? commandName : "";
	if (command == "Game.BuildSupplyStashSmart")
	{
		return slots.supply;
	}
	if (command == "Game.BuildBarracksSmart")
	{
		return slots.barracks;
	}
	if (command == "Game.BuildArmsDealerSmart")
	{
		return slots.armsDealer;
	}
	if (command == "Game.BuildPalaceSmart")
	{
		return slots.palace;
	}
	if (command == "Game.BuildBlackMarketSmart")
	{
		return slots.blackMarket;
	}
	if (command == "Game.BuildTunnelNetwork")
	{
		return slots.tunnel;
	}
	if (command == "Game.BuildStingerSite")
	{
		return slots.stinger;
	}
	return nullptr;
}

const unsigned long* AIControlAdapterMacroBuildDispatcher::FindBuildCooldownTick(
	const AIControlAdapterMacroBuildCooldownSlots& slots,
	const char* commandName)
{
	AIControlAdapterMacroBuildCooldownSlots mutableSlots;
	mutableSlots.supply = slots.supply;
	mutableSlots.barracks = slots.barracks;
	mutableSlots.armsDealer = slots.armsDealer;
	mutableSlots.palace = slots.palace;
	mutableSlots.blackMarket = slots.blackMarket;
	mutableSlots.tunnel = slots.tunnel;
	mutableSlots.stinger = slots.stinger;
	return FindBuildCooldownTick(mutableSlots, commandName);
}

bool AIControlAdapterMacroBuildDispatcher::IsBuildCooldownReady(
	const AIControlAdapterMacroBuildCooldownSlots& slots,
	const char* commandName,
	unsigned long now)
{
	const unsigned long* nextAllowedTick = FindBuildCooldownTick(slots, commandName);
	return nextAllowedTick == nullptr || hasTickElapsed(*nextAllowedTick, now);
}

bool AIControlAdapterMacroBuildDispatcher::IsBuildAttemptReady(
	const AIControlAdapterMacroBuildCooldownSlots& slots,
	const char* commandName,
	int inProgressCount,
	unsigned long now)
{
	if (inProgressCount > 0)
	{
		return false;
	}
	return IsBuildCooldownReady(slots, commandName, now);
}

void AIControlAdapterMacroBuildDispatcher::RecordBuildAttempt(
	AIControlAdapterMacroBuildCooldownSlots& slots,
	const char* commandName,
	bool success,
	const std::string& reason,
	unsigned long now)
{
	unsigned long* nextAllowedTick = FindBuildCooldownTick(slots, commandName);
	if (nextAllowedTick == nullptr)
	{
		return;
	}
	const unsigned int delayMs = AIControlAdapterGetBuildRetryDelayMs(commandName, success, reason.c_str());
	*nextAllowedTick = now + delayMs;
}

AIControlAdapterMacroBuildCommandAlias AIControlAdapterMacroBuildDispatcher::ResolveCommandAlias(
	const char* commandName)
{
	AIControlAdapterMacroBuildCommandAlias alias;
	alias.command = safeString(commandName, "");
	if (alias.command == "Game.BuildTunnelNetwork")
	{
		alias.command = "Game.BuildBarracksSmart";
		alias.buildingTemplate = "GLATunnelNetwork";
		alias.hasBuildingTemplate = true;
	}
	else if (alias.command == "Game.BuildStingerSite")
	{
		alias.command = "Game.BuildBarracksSmart";
		alias.buildingTemplate = "GLAStingerSite";
		alias.hasBuildingTemplate = true;
	}
	return alias;
}
