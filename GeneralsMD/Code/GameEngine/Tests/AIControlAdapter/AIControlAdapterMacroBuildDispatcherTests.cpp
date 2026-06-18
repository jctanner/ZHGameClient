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

	{
		unsigned long supply = 0u;
		unsigned long barracks = 0u;
		unsigned long unknown = 0u;
		AIControlAdapterMacroBuildCooldownSlots slots;
		slots.supply = &supply;
		slots.barracks = &barracks;

		expect(
			AIControlAdapterMacroBuildDispatcher::FindBuildCooldownTick(slots, "Game.BuildSupplyStashSmart") == &supply,
			"Supply build command should map to supply cooldown slot");
		expect(
			AIControlAdapterMacroBuildDispatcher::FindBuildCooldownTick(slots, "Game.BuildBarracksSmart") == &barracks,
			"Barracks build command should map to barracks cooldown slot");
		expect(
			AIControlAdapterMacroBuildDispatcher::FindBuildCooldownTick(slots, "Game.BuildUnknown") == nullptr,
			"Unknown build command should not map to a cooldown slot");
		expect(
			AIControlAdapterMacroBuildDispatcher::FindBuildCooldownTick(slots, nullptr) == nullptr,
			"Null build command should not map to a cooldown slot");

		unknown = 2000u;
		(void)unknown;
	}

	{
		unsigned long stinger = 12000u;
		AIControlAdapterMacroBuildCooldownSlots slots;
		slots.stinger = &stinger;

		expect(
			!AIControlAdapterMacroBuildDispatcher::IsBuildCooldownReady(slots, "Game.BuildStingerSite", 11000u),
			"Future cooldown should not be ready");
		expect(
			AIControlAdapterMacroBuildDispatcher::IsBuildCooldownReady(slots, "Game.BuildStingerSite", 12000u),
			"Cooldown should be ready at deadline");
		expect(
			AIControlAdapterMacroBuildDispatcher::IsBuildCooldownReady(slots, "Game.BuildStingerSite", 13000u),
			"Cooldown should be ready after deadline");
		expect(
			AIControlAdapterMacroBuildDispatcher::IsBuildCooldownReady(slots, "Game.BuildUnknown", 11000u),
			"Unknown cooldown command should be ready by default");
	}

	{
		unsigned long tunnel = 0u;
		AIControlAdapterMacroBuildCooldownSlots slots;
		slots.tunnel = &tunnel;

		expect(
			!AIControlAdapterMacroBuildDispatcher::IsBuildAttemptReady(slots, "Game.BuildTunnelNetwork", 1, 1000u),
			"In-progress builds should block additional attempts");
		expect(
			AIControlAdapterMacroBuildDispatcher::IsBuildAttemptReady(slots, "Game.BuildTunnelNetwork", 0, 1000u),
			"Ready cooldown with no in-progress builds should allow attempt");
	}

	{
		unsigned long supply = 0u;
		unsigned long palace = 0u;
		AIControlAdapterMacroBuildCooldownSlots slots;
		slots.supply = &supply;
		slots.palace = &palace;

		AIControlAdapterMacroBuildDispatcher::RecordBuildAttempt(
			slots,
			"Game.BuildSupplyStashSmart",
			false,
			"construct_site_not_created",
			10000u);
		expect(supply == 12500u, "Supply construct-site failure should record aggressive retry delay");

		AIControlAdapterMacroBuildDispatcher::RecordBuildAttempt(
			slots,
			"Game.BuildPalaceSmart",
			true,
			"",
			10000u);
		expect(palace == 22000u, "Palace success should record long retry delay");
	}

	{
		const AIControlAdapterMacroBuildCommandAlias passthrough =
			AIControlAdapterMacroBuildDispatcher::ResolveCommandAlias("Game.BuildBarracksSmart");
		expect(passthrough.command == "Game.BuildBarracksSmart", "Normal command aliases should preserve command");
		expect(!passthrough.hasBuildingTemplate, "Normal command aliases should not add building template");

		const AIControlAdapterMacroBuildCommandAlias tunnel =
			AIControlAdapterMacroBuildDispatcher::ResolveCommandAlias("Game.BuildTunnelNetwork");
		expect(tunnel.command == "Game.BuildBarracksSmart", "Tunnel alias should execute through barracks smart build");
		expect(tunnel.hasBuildingTemplate, "Tunnel alias should provide a building template");
		expect(tunnel.buildingTemplate == "GLATunnelNetwork", "Tunnel alias should use tunnel template");

		const AIControlAdapterMacroBuildCommandAlias stinger =
			AIControlAdapterMacroBuildDispatcher::ResolveCommandAlias("Game.BuildStingerSite");
		expect(stinger.command == "Game.BuildBarracksSmart", "Stinger alias should execute through barracks smart build");
		expect(stinger.hasBuildingTemplate, "Stinger alias should provide a building template");
		expect(stinger.buildingTemplate == "GLAStingerSite", "Stinger alias should use stinger template");
	}

	std::cout << "AIControlAdapterMacroBuildDispatcherTests passed\n";
	return 0;
}
