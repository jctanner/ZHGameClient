#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendSnapshotBuilder.h"

AIControlAdapterStrategicSpendSnapshotBase AIControlAdapterStrategicSpendSnapshotBuilder::BuildBase(
	const AIControlAdapterStrategicSpendSnapshotBaseInput& input) const
{
	AIControlAdapterStrategicSpendEconomyFacts economy;
	economy.money = input.money;
	economy.reserveCash = input.reserveCash;
	economy.completedMarkets = input.completedMarkets;
	economy.healthyMarketsInProgress = input.healthyMarketsInProgress;
	economy.staleMarketFoundations = input.staleMarketFoundations;
	economy.staleStrategicFoundations = input.staleStrategicFoundations;
	economy.activeWmdThreats = input.activeWmdThreats;
	economy.mainBaseCritical = input.mainBaseCritical;

	AIControlAdapterStrategicSpendZoneFacts zones;
	zones.currentZones = input.currentZones;
	zones.developedZones = input.developedZones;
	zones.desiredZones = input.desiredZones;

	AIControlAdapterStrategicSpendArmyFacts army;
	army.armySize = input.armySize;
	army.armyCap = input.armyCap;
	army.quads = input.quads;
	army.buggies = input.buggies;
	army.scorpions = input.scorpions;

	return BuildBase(economy, zones, army);
}

AIControlAdapterStrategicSpendSnapshotBase AIControlAdapterStrategicSpendSnapshotBuilder::BuildBase(
	const AIControlAdapterStrategicSpendEconomyFacts& economy,
	const AIControlAdapterStrategicSpendZoneFacts& zones,
	const AIControlAdapterStrategicSpendArmyFacts& army) const
{
	AIControlAdapterStrategicSpendSnapshotBase base;
	base.money = economy.money;
	base.reserveCash = economy.reserveCash;
	base.currentZones = zones.currentZones;
	base.developedZones = zones.developedZones;
	base.desiredZones = zones.desiredZones;
	base.completedMarkets = economy.completedMarkets;
	base.healthyMarketsInProgress = economy.healthyMarketsInProgress;
	base.staleMarketFoundations = economy.staleMarketFoundations;
	base.staleStrategicFoundations = economy.staleStrategicFoundations;
	base.activeWmdThreats = economy.activeWmdThreats;
	base.armySize = army.armySize;
	base.armyCap = army.armyCap;
	base.quads = army.quads;
	base.buggies = army.buggies;
	base.scorpions = army.scorpions;
	base.mainBaseCritical = economy.mainBaseCritical;
	return base;
}

AIControlAdapterStrategicSpendSnapshot AIControlAdapterStrategicSpendSnapshotBuilder::Build(
	const AIControlAdapterStrategicSpendSnapshotBase& base,
	const AIControlAdapterStrategicSpendSnapshotOverrides& overrides) const
{
	AIControlAdapterStrategicSpendSnapshot snapshot;
	snapshot.money = base.money;
	snapshot.reserveCash = base.reserveCash;
	snapshot.currentZones = base.currentZones;
	snapshot.developedZones = base.developedZones;
	snapshot.desiredZones = base.desiredZones;
	snapshot.completedMarkets = base.completedMarkets;
	snapshot.healthyMarketsInProgress = base.healthyMarketsInProgress;
	snapshot.staleMarketFoundations = base.staleMarketFoundations;
	snapshot.staleStrategicFoundations = base.staleStrategicFoundations;
	snapshot.activeLocalEnemies = overrides.activeLocalEnemies;
	snapshot.activeWmdThreats = base.activeWmdThreats;
	snapshot.armySize = base.armySize;
	snapshot.armyCap = base.armyCap;
	snapshot.quads = base.quads;
	snapshot.buggies = base.buggies;
	snapshot.scorpions = base.scorpions;
	snapshot.mainBaseCritical = base.mainBaseCritical;
	snapshot.emergencySurvivalActive = overrides.emergencySurvivalActive;
	snapshot.expansionUrgent = overrides.expansionUrgent;
	snapshot.incomeCritical = overrides.incomeCritical;
	snapshot.reserveDepleted = overrides.reserveDepleted;
	return snapshot;
}
