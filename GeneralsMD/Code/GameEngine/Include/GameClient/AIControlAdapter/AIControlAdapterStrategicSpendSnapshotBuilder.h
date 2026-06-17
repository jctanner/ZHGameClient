/**
 * AIControlAdapterStrategicSpendSnapshotBuilder.h
 *
 * Shared construction of strategic spend policy snapshots.
 */

#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterStrategicSpendManager.h"

struct AIControlAdapterStrategicSpendSnapshotBase
{
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	int currentZones = 0;
	int developedZones = 0;
	int desiredZones = 0;
	int completedMarkets = 0;
	int healthyMarketsInProgress = 0;
	int staleMarketFoundations = 0;
	int staleStrategicFoundations = 0;
	int activeWmdThreats = 0;
	int armySize = 0;
	int armyCap = 0;
	int quads = 0;
	int buggies = 0;
	int scorpions = 0;
	bool mainBaseCritical = false;
};

struct AIControlAdapterStrategicSpendSnapshotBaseInput
{
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	int currentZones = 0;
	int developedZones = 0;
	int desiredZones = 0;
	int completedMarkets = 0;
	int healthyMarketsInProgress = 0;
	int staleMarketFoundations = 0;
	int staleStrategicFoundations = 0;
	int activeWmdThreats = 0;
	int armySize = 0;
	int armyCap = 0;
	int quads = 0;
	int buggies = 0;
	int scorpions = 0;
	bool mainBaseCritical = false;
};

struct AIControlAdapterStrategicSpendEconomyFacts
{
	unsigned int money = 0u;
	unsigned int reserveCash = 0u;
	int completedMarkets = 0;
	int healthyMarketsInProgress = 0;
	int staleMarketFoundations = 0;
	int staleStrategicFoundations = 0;
	int activeWmdThreats = 0;
	bool mainBaseCritical = false;
};

struct AIControlAdapterStrategicSpendZoneFacts
{
	int currentZones = 0;
	int developedZones = 0;
	int desiredZones = 0;
};

struct AIControlAdapterStrategicSpendArmyFacts
{
	int armySize = 0;
	int armyCap = 0;
	int quads = 0;
	int buggies = 0;
	int scorpions = 0;
};

struct AIControlAdapterStrategicSpendSnapshotOverrides
{
	int activeLocalEnemies = 0;
	bool emergencySurvivalActive = false;
	bool expansionUrgent = false;
	bool incomeCritical = false;
	bool reserveDepleted = false;
};

class AIControlAdapterStrategicSpendSnapshotBuilder
{
public:
	AIControlAdapterStrategicSpendSnapshotBase BuildBase(
		const AIControlAdapterStrategicSpendSnapshotBaseInput& input) const;
	AIControlAdapterStrategicSpendSnapshotBase BuildBase(
		const AIControlAdapterStrategicSpendEconomyFacts& economy,
		const AIControlAdapterStrategicSpendZoneFacts& zones,
		const AIControlAdapterStrategicSpendArmyFacts& army) const;
	AIControlAdapterStrategicSpendSnapshot Build(
		const AIControlAdapterStrategicSpendSnapshotBase& base,
		const AIControlAdapterStrategicSpendSnapshotOverrides& overrides) const;
};
