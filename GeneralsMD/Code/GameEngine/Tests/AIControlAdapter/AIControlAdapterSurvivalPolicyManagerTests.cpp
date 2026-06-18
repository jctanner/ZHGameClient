#include "GameClient/AIControlAdapter/AIControlAdapterStrategicFoundationSurvivalManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterSurvivalPolicyManager.h"

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
}

int main()
{
	{
		const AIControlAdapterSurvivalPolicyResult result =
			AIControlAdapterSurvivalPolicyManager().Evaluate({
				0,
				0,
				2,
				1,
				0,
				1,
				1,
				50000u,
				10000u
			});
		expect(result.state == std::string("stable"), "Stable facts should produce stable state");
		expect(result.priority == std::string("normal_macro"), "Stable state should keep normal macro");
		expect(!result.blockExposedWmdFoundations, "Stable state should not block WMD foundations");
	}

	{
		const AIControlAdapterSurvivalPolicyResult result =
			AIControlAdapterSurvivalPolicyManager().Evaluate({
				1,
				3,
				2,
				1,
				0,
				1,
				1,
				50000u,
				10000u
			});
		expect(result.state == std::string("under_pressure"), "Defense deficits should produce under-pressure state");
		expect(result.priority == std::string("stabilize_front"), "Under pressure should stabilize front");
	}

	{
		const AIControlAdapterSurvivalPolicyResult result =
			AIControlAdapterSurvivalPolicyManager().Evaluate({
				0,
				0,
				2,
				0,
				2,
				1,
				1,
				50000u,
				10000u
			});
		expect(result.state == std::string("wmd_crisis"), "Enemy WMDs with no ready SCUD should produce WMD crisis");
		expect(result.wmdCrisis, "WMD crisis flag should be set");
		expect(result.priority == std::string("protected_wmd_response"), "WMD crisis should prioritize protected response");
	}

	{
		const AIControlAdapterSurvivalPolicyResult result =
			AIControlAdapterSurvivalPolicyManager().Evaluate({
				0,
				0,
				2,
				1,
				0,
				1,
				0,
				50000u,
				10000u
			});
		expect(result.state == std::string("producer_spine_broken"), "Missing Arms Dealer should break producer spine");
		expect(result.producerSpineBroken, "Producer spine flag should be set");
		expect(result.priority == std::string("producer_recovery"), "Producer spine should prioritize recovery");
	}

	{
		const AIControlAdapterSurvivalPolicyResult result =
			AIControlAdapterSurvivalPolicyManager().Evaluate({
				2,
				8,
				0,
				0,
				2,
				1,
				1,
				9000u,
				10000u
			});
		expect(result.state == std::string("collapse_imminent"), "Critical zones with no combat tasks should be collapse imminent");
		expect(result.collapseImminent, "Collapse flag should be set");
		expect(result.blockExposedWmdFoundations, "Collapse should block exposed WMD foundations");
	}

	{
		AIControlAdapterStrategicFoundationFact fact;
		fact.templateName = "GLAScudStorm";
		fact.reason = "destroyed";
		const AIControlAdapterStrategicFoundationClassification result =
			AIControlAdapterStrategicFoundationSurvivalManager().Classify(fact);
		expect(result.state == std::string("destroyed"), "Destroyed foundation should classify as destroyed");
		expect(result.failed, "Destroyed foundation should be failed");
		expect(result.rebuildBlocked, "Destroyed SCUD foundation should be rebuild-block eligible");
	}

	{
		AIControlAdapterStrategicFoundationFact fact;
		fact.templateName = "GLAScudStorm";
		fact.reason = "no_active_builder";
		fact.recoveryAttempts = 2;
		const AIControlAdapterStrategicFoundationClassification result =
			AIControlAdapterStrategicFoundationSurvivalManager().Classify(fact);
		expect(result.state == std::string("no_builder"), "No-builder foundation should classify explicitly");
		expect(result.failed, "Repeated no-builder recovery should be failed");
	}

	{
		AIControlAdapterStrategicFoundationFact failed;
		failed.foundationId = 42u;
		failed.templateName = "GLAScudStorm";
		failed.reason = "stopped_stale_no_progress";
		failed.stopIssued = true;
		failed.x = 1000.0f;
		failed.y = 1000.0f;
		failed.lastSeenTick = 10000u;
		const AIControlAdapterScudStormRebuildBlockResult result =
			AIControlAdapterStrategicFoundationSurvivalManager().ShouldBlockScudStormRebuild({
				1120.0f,
				980.0f,
				12000u,
				300000u,
				650.0f,
				false,
				{ failed }
			});
		expect(result.blocked, "Recent failed SCUD foundation should block nearby rebuild");
		expect(result.foundationId == 42u, "Rebuild block should expose source foundation");
		expect(result.reason == std::string("recent_failed_scud_foundation"), "Rebuild block should expose concrete reason");
	}

	{
		AIControlAdapterStrategicFoundationFact failed;
		failed.foundationId = 43u;
		failed.templateName = "GLAScudStorm";
		failed.reason = "destroyed";
		failed.x = 1000.0f;
		failed.y = 1000.0f;
		failed.lastSeenTick = 10000u;
		const AIControlAdapterScudStormRebuildBlockResult result =
			AIControlAdapterStrategicFoundationSurvivalManager().ShouldBlockScudStormRebuild({
				3000.0f,
				3000.0f,
				12000u,
				300000u,
				650.0f,
				true,
				{ failed }
			});
		expect(!result.blocked, "Distant failed SCUD foundation should not block rebuild");
	}

	{
		AIControlAdapterStrategicFoundationFact fact;
		fact.foundationId = 6343u;
		fact.templateName = "GLAScudStorm";
		fact.reason = "stopped_stale_no_progress";
		fact.stopIssued = true;
		fact.tombstoned = true;
		fact.activeWmdThreat = true;
		fact.lastHealth = 2800.0f;
		fact.maxHealth = 4000.0f;
		const AIControlAdapterStrategicFoundationClassification result =
			AIControlAdapterStrategicFoundationSurvivalManager().Classify(fact);
		expect(result.state == std::string("recoverable"), "High-progress tombstoned SCUD under WMD pressure should be recoverable");
		expect(result.recoverable, "Recoverable flag should be set for high-progress SCUD");
		expect(!result.failed, "Recoverable SCUD foundation should not be classified failed");
		expect(!result.rebuildBlocked, "Recoverable SCUD foundation should not block its own recovery");
		expect(result.tombstoned, "Tombstone state should be surfaced");
	}

	{
		AIControlAdapterStrategicFoundationFact fact;
		fact.foundationId = 6522u;
		fact.templateName = "GLAScudStorm";
		fact.reason = "stopped_stale_no_progress";
		fact.stopIssued = true;
		fact.tombstoned = true;
		fact.activeWmdThreat = true;
		fact.lastHealth = 60.0f;
		fact.maxHealth = 4000.0f;
		const AIControlAdapterStrategicFoundationClassification result =
			AIControlAdapterStrategicFoundationSurvivalManager().Classify(fact);
		expect(result.state == std::string("stopped"), "Low-health tombstoned SCUD should remain stopped");
		expect(!result.recoverable, "Low-health tombstoned SCUD should not be recoverable");
		expect(result.failed, "Low-health stopped SCUD should still be failed");
		expect(result.rebuildBlocked, "Low-health stopped SCUD should still block nearby rebuild churn");
	}

	std::cout << "AIControlAdapterSurvivalPolicyManagerTests passed\n";
	return 0;
}
