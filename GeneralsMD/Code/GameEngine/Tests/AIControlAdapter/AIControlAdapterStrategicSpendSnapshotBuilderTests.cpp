#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendSnapshotBuilder.h"

#include <cstdlib>
#include <iostream>

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

	AIControlAdapterStrategicSpendSnapshotBase makeBase()
	{
		AIControlAdapterStrategicSpendSnapshotBase base;
		base.money = 50000u;
		base.reserveCash = 10000u;
		base.currentZones = 7;
		base.developedZones = 4;
		base.desiredZones = 30;
		base.completedMarkets = 3;
		base.healthyMarketsInProgress = 1;
		base.staleMarketFoundations = 2;
		base.staleStrategicFoundations = 5;
		base.activeWmdThreats = 1;
		base.armySize = 42;
		base.armyCap = 60;
		base.quads = 12;
		base.buggies = 4;
		base.scorpions = 8;
		base.mainBaseCritical = true;
		return base;
	}
}

int main()
{
	const AIControlAdapterStrategicSpendSnapshotBuilder builder;

	{
		AIControlAdapterStrategicSpendSnapshotBaseInput input;
		input.money = 12000u;
		input.reserveCash = 5000u;
		input.currentZones = 5;
		input.developedZones = 2;
		input.desiredZones = 14;
		input.completedMarkets = 1;
		input.healthyMarketsInProgress = 3;
		input.staleMarketFoundations = 4;
		input.staleStrategicFoundations = 6;
		input.activeWmdThreats = 1;
		input.armySize = 22;
		input.armyCap = 44;
		input.quads = 7;
		input.buggies = 2;
		input.scorpions = 5;
		input.mainBaseCritical = true;
		const AIControlAdapterStrategicSpendSnapshotBase base = builder.BuildBase(input);
		expect(base.money == 12000u, "Base should preserve money");
		expect(base.reserveCash == 5000u, "Base should preserve reserve");
		expect(base.currentZones == 5, "Base should preserve current zones");
		expect(base.developedZones == 2, "Base should preserve developed zones");
		expect(base.desiredZones == 14, "Base should preserve desired zones");
		expect(base.completedMarkets == 1, "Base should preserve completed markets");
		expect(base.healthyMarketsInProgress == 3, "Base should preserve in-progress markets");
		expect(base.staleMarketFoundations == 4, "Base should preserve stale market foundations");
		expect(base.staleStrategicFoundations == 6, "Base should preserve stale strategic foundations");
		expect(base.activeWmdThreats == 1, "Base should preserve active WMD threats");
		expect(base.armySize == 22, "Base should preserve army size");
		expect(base.armyCap == 44, "Base should preserve army cap");
		expect(base.quads == 7, "Base should preserve quads");
		expect(base.buggies == 2, "Base should preserve buggies");
		expect(base.scorpions == 5, "Base should preserve scorpions");
		expect(base.mainBaseCritical, "Base should preserve main-base critical flag");
	}

	{
		AIControlAdapterStrategicSpendEconomyFacts economy;
		economy.money = 30000u;
		economy.reserveCash = 10000u;
		economy.completedMarkets = 2;
		economy.healthyMarketsInProgress = 1;
		economy.staleMarketFoundations = 3;
		economy.staleStrategicFoundations = 4;
		economy.activeWmdThreats = 1;
		economy.mainBaseCritical = true;

		AIControlAdapterStrategicSpendZoneFacts zones;
		zones.currentZones = 8;
		zones.developedZones = 5;
		zones.desiredZones = 30;

		AIControlAdapterStrategicSpendArmyFacts army;
		army.armySize = 35;
		army.armyCap = 60;
		army.quads = 10;
		army.buggies = 6;
		army.scorpions = 9;

		const AIControlAdapterStrategicSpendSnapshotBase base = builder.BuildBase(economy, zones, army);
		expect(base.money == 30000u, "Fact base should preserve money");
		expect(base.reserveCash == 10000u, "Fact base should preserve reserve");
		expect(base.currentZones == 8, "Fact base should preserve current zones");
		expect(base.developedZones == 5, "Fact base should preserve developed zones");
		expect(base.desiredZones == 30, "Fact base should preserve desired zones");
		expect(base.completedMarkets == 2, "Fact base should preserve completed markets");
		expect(base.healthyMarketsInProgress == 1, "Fact base should preserve healthy market foundations");
		expect(base.staleMarketFoundations == 3, "Fact base should preserve stale market foundations");
		expect(base.staleStrategicFoundations == 4, "Fact base should preserve stale strategic foundations");
		expect(base.activeWmdThreats == 1, "Fact base should preserve WMD pressure");
		expect(base.armySize == 35, "Fact base should preserve army size");
		expect(base.armyCap == 60, "Fact base should preserve army cap");
		expect(base.quads == 10, "Fact base should preserve quads");
		expect(base.buggies == 6, "Fact base should preserve buggies");
		expect(base.scorpions == 9, "Fact base should preserve scorpions");
		expect(base.mainBaseCritical, "Fact base should preserve main-base critical flag");
	}

	{
		AIControlAdapterStrategicSpendSnapshotOverrides overrides;
		overrides.expansionUrgent = true;
		overrides.incomeCritical = false;
		overrides.reserveDepleted = false;
		const AIControlAdapterStrategicSpendSnapshot snapshot = builder.Build(makeBase(), overrides);
		expect(snapshot.money == 50000u, "Snapshot should preserve money");
		expect(snapshot.reserveCash == 10000u, "Snapshot should preserve reserve");
		expect(snapshot.currentZones == 7, "Snapshot should preserve current zones");
		expect(snapshot.developedZones == 4, "Snapshot should preserve developed zones");
		expect(snapshot.desiredZones == 30, "Snapshot should preserve desired zones");
		expect(snapshot.completedMarkets == 3, "Snapshot should preserve market count");
		expect(snapshot.healthyMarketsInProgress == 1, "Snapshot should preserve healthy market foundations");
		expect(snapshot.staleMarketFoundations == 2, "Snapshot should preserve stale market foundations");
		expect(snapshot.staleStrategicFoundations == 5, "Snapshot should preserve stale strategic foundations");
		expect(snapshot.activeWmdThreats == 1, "Snapshot should preserve WMD pressure");
		expect(snapshot.armySize == 42, "Snapshot should preserve army size");
		expect(snapshot.armyCap == 60, "Snapshot should preserve army cap");
		expect(snapshot.quads == 12, "Snapshot should preserve quads");
		expect(snapshot.buggies == 4, "Snapshot should preserve buggies");
		expect(snapshot.scorpions == 8, "Snapshot should preserve scorpions");
		expect(snapshot.mainBaseCritical, "Snapshot should preserve main-base critical flag");
		expect(snapshot.expansionUrgent, "Snapshot should preserve expansion override");
		expect(!snapshot.incomeCritical, "Snapshot should preserve income override");
	}

	{
		AIControlAdapterStrategicSpendSnapshotBase base = makeBase();
		base.money = 900u;
		base.currentZones = 1;
		base.completedMarkets = 0;
		AIControlAdapterStrategicSpendSnapshotOverrides overrides;
		overrides.activeLocalEnemies = 9;
		overrides.emergencySurvivalActive = true;
		overrides.incomeCritical = true;
		overrides.reserveDepleted = true;
		const AIControlAdapterStrategicSpendSnapshot snapshot = builder.Build(base, overrides);
		expect(snapshot.activeLocalEnemies == 9, "Snapshot should preserve local enemy override");
		expect(snapshot.emergencySurvivalActive, "Snapshot should preserve emergency override");
		expect(snapshot.incomeCritical, "Snapshot should preserve critical income override");
		expect(snapshot.reserveDepleted, "Snapshot should preserve reserve override");
	}

	std::cout << "AIControlAdapterStrategicSpendSnapshotBuilderTests passed\n";
	return 0;
}
