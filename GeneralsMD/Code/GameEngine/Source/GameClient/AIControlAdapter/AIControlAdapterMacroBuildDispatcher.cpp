#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildDispatcher.h"

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
