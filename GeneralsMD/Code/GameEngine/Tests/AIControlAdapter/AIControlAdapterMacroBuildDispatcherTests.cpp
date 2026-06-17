#include "GameClient/AIControlAdapter/AIControlAdapterMacroBuildDispatcher.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << "\n";
			std::exit(1);
		}
	}

	AIControlAdapterMacroBuildPlan makePlan(AIControlAdapterMacroBuildExecutor executor)
	{
		AIControlAdapterMacroBuildPlan plan;
		AIControlAdapterMacroBuildIntent intent;
		intent.option.category = "static_defense";
		intent.option.command = "Game.BuildStingerSite";
		intent.option.reason = "frontier_floor";
		intent.option.priority = 80;
		intent.option.valid = true;
		intent.executor = executor;
		intent.preferZone = true;
		intent.zoneIndex = 3;
		intent.taskName = "auto_static_defense";
		plan.intents.push_back(intent);
		plan.choice.index = 0;
		plan.choice.category = intent.option.category;
		plan.choice.command = intent.option.command;
		plan.choice.reason = intent.option.reason;
		plan.choice.priority = intent.option.priority;
		return plan;
	}
}

int main()
{
	const AIControlAdapterMacroBuildDispatcher dispatcher;

	{
		AIControlAdapterMacroBuildPlan plan;
		plan.choice.index = -1;
		plan.choice.reason = "no_valid_intent";
		const AIControlAdapterMacroBuildDispatchRequest request = dispatcher.BuildDispatch(plan);
		expect(!request.shouldDispatch, "No selected intent should not dispatch");
		expect(request.reason == "no_valid_intent", "No selected intent should preserve reason");
	}

	{
		const AIControlAdapterMacroBuildDispatchRequest request =
			dispatcher.BuildDispatch(makePlan(AIControlAdapterMacroBuildExecutor::SpecificZone));
		expect(request.shouldDispatch, "Valid selected intent should dispatch");
		expect(request.kind == AIControlAdapterMacroBuildDispatchKind::SpecificZone, "Specific-zone executor should map to specific-zone dispatch");
		expect(request.command == "Game.BuildStingerSite", "Dispatch should preserve command");
		expect(request.category == "static_defense", "Dispatch should preserve category");
		expect(request.reason == "frontier_floor", "Dispatch should preserve selected reason");
		expect(request.priority == 80, "Dispatch should preserve priority");
		expect(request.preferZone, "Dispatch should preserve zone preference");
		expect(request.zoneIndex == 3, "Dispatch should preserve zone index");
		expect(request.taskName == "auto_static_defense", "Dispatch should preserve task name");
	}

	{
		const AIControlAdapterMacroBuildDispatchRequest request =
			dispatcher.BuildDispatch(makePlan(AIControlAdapterMacroBuildExecutor::SupplyExpansion));
		expect(request.kind == AIControlAdapterMacroBuildDispatchKind::SupplyExpansion, "Supply expansion executor should map to supply dispatch");
		const AIControlAdapterMacroBuildDispatchResult result = dispatcher.CompleteDispatch(request, false, "");
		expect(result.attempted, "Completed valid dispatch should be attempted");
		expect(!result.issued, "Completed dispatch should preserve issued flag");
		expect(result.reason == "frontier_floor", "Empty execution reason should fall back to selected reason");
	}

	{
		const AIControlAdapterMacroBuildDispatchRequest request =
			dispatcher.BuildDispatch(makePlan(AIControlAdapterMacroBuildExecutor::MacroBuild));
		expect(request.kind == AIControlAdapterMacroBuildDispatchKind::MacroBuild, "Macro executor should map to macro dispatch");
		const AIControlAdapterMacroBuildDispatchResult result = dispatcher.CompleteDispatch(request, true, "placement_ok");
		expect(result.issued, "Completed dispatch should preserve successful issued flag");
		expect(result.reason == "placement_ok", "Execution reason should override selected reason");
	}

	std::cout << "AIControlAdapterMacroBuildDispatcherTests passed\n";
	return 0;
}
