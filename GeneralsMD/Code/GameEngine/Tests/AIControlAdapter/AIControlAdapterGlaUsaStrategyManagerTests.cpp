#include "GameClient/AIControlAdapter/AIControlAdapterGlaUsaStrategyManager.h"

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

	AIControlAdapterGlaUsaStrategyInput baseUsaInput()
	{
		AIControlAdapterGlaUsaStrategyInput input;
		input.enemyUsaDetected = true;
		input.money = 25000u;
		input.reserveCash = 10000u;
		input.completedPalaces = 1;
		input.readyBarracks = 2;
		input.readyArmsDealers = 2;
		input.tunnels = 6;
		input.stingers = 4;
		input.exposedExpansionZones = 3;
		input.workers = 12;
		input.technicals = 2;
		return input;
	}
}

int main()
{
	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.enemyArmorThreats = 5;
		input.quads = 9;
		input.scorpions = 3;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.active, "USA enemy should activate strategy policy");
		expect(result.pressure == std::string("armor"), "Armor facts should classify armor pressure");
		expect(result.scorpionFloor > result.quadFloor, "Armor pressure should favor Scorpion floor");
		expect(result.preferScorpionProduction, "Paladin-style armor pressure should prefer Scorpion production");
		expect(!result.tunnelMissilesSatisfyArmor, "Tunnel missiles should not satisfy USA armor response");
		expect(result.tunnelRole == std::string("logistics_ambush_anchor"), "Tunnels should be logistics/ambush anchors");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.enemyAirThreats = 4;
		input.quads = 2;
		input.scorpions = 10;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.pressure == std::string("air"), "Air facts should classify air pressure");
		expect(result.quadFloor > result.scorpionFloor, "Air pressure should favor Quad floor");
		expect(result.preferQuadProduction, "Air pressure should prefer Quad production when below floor");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.enemyUsaWmdTargets = 2;
		input.enemyArmorThreats = 2;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.pressure == std::string("mixed_wmd"), "USA WMD plus combat pressure should classify mixed WMD");
		expect(result.wmdConstructionDiagnosticNeeded, "Enemy WMD should require construction diagnostics");
		expect(result.wmdConstructionReason == std::string("enemy_usa_wmd_detected"), "WMD diagnostic should expose reason");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.completedPalaces = 0;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(!result.camouflageDesired, "Camouflage should not be desired without Palace");
		expect(!result.camouflageSpendAllowed, "Camouflage spend should be blocked without Palace");
		expect(result.camouflageReason == std::string("palace_missing"), "Camouflage should explain Palace gate");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.money = 14500u;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.camouflageDesired, "Enough anchors should desire camouflage");
		expect(!result.camouflageSpendAllowed, "Low cash float should block camouflage spend");
		expect(result.camouflageReason == std::string("cash_float_low"), "Camouflage should protect cash float");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.producerSpineBroken = true;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.camouflageDesired, "Anchor count can still desire camouflage during producer issues");
		expect(!result.camouflageSpendAllowed, "Producer spine should block camouflage spend");
		expect(result.camouflageReason == std::string("producer_spine_priority"), "Camouflage should explain producer priority");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.enemyArmorThreats = 1;
		input.money = 24000u;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.camouflageDesired, "Camouflage should be desired with Palace and anchors");
		expect(result.camouflageSpendAllowed, "Camouflage should be allowed with protected reserve and stable spine");
		expect(result.camouflageReason == std::string("usa_anchor_multiplier"), "Camouflage should expose payoff reason");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.remoteBuildGap = 5;
		input.farthestRemoteBuildDistance = 1800.0f;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.workerMobilityDesired, "Far remote build gap should desire worker mobility");
		expect(result.workerMobilityMode == std::string("technical"), "Available Technicals should choose Technical shuttle mode");
		expect(result.desiredShuttleTechnicals == 2, "Large remote gap should desire two shuttle Technicals");
	}

	{
		AIControlAdapterGlaUsaStrategyInput input = baseUsaInput();
		input.remoteBuildGap = 3;
		input.farthestRemoteBuildDistance = 1800.0f;
		input.technicals = 0;
		input.queuedTechnicals = 0;
		const AIControlAdapterGlaUsaStrategyResult result =
			AIControlAdapterGlaUsaStrategyManager().Evaluate(input);
		expect(result.workerMobilityDesired, "Remote build gap should still desire mobility without Technicals");
		expect(result.workerMobilityMode == std::string("mixed"), "Tunnel network should produce mixed mobility recommendation");
		expect(result.workerMobilityReason == std::string("needs_more_technicals"), "Policy should explain Technical shortage");
	}

	std::cout << "AIControlAdapterGlaUsaStrategyManagerTests passed\n";
	return 0;
}
