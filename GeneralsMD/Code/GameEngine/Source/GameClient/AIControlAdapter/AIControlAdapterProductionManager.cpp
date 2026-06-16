/**
 * AIControlAdapterProductionManager.cpp
 *
 * Implementation of production management system.
 *
 * See AIControlAdapterProductionManager.h for design principles and architecture.
 */

#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterProductionManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <cmath>
#include <algorithm>
#include <cstring>

AIControlAdapterProductionManager::AIControlAdapterProductionManager()
{
}

void AIControlAdapterProductionManager::Reset()
{
	// Production manager is stateless - nothing to reset
}

int AIControlAdapterProductionManager::CalculateEffectiveArmyCap(const ProductionManagerInputs& inputs)
{
	// Use existing policy function
	const int baseMaxCap = inputs.isBalancedSprawl ? 100 : 9999;
	return AIControlAdapterGetEffectiveArmyCap({
		inputs.isBalancedSprawl,
		inputs.money,
		inputs.incomePerMinute,
		inputs.barracks,
		inputs.armsDealers,
		inputs.blackMarkets,
		baseMaxCap
	});
}

bool AIControlAdapterProductionManager::ShouldPauseCombatProduction(const ProductionManagerInputs& inputs)
{
	// Use existing policy function
	return AIControlAdapterShouldPauseCombatProduction({
		inputs.isBalancedSprawl,
		inputs.openingInfrastructureReady,
		inputs.openingEconomyReady,
		inputs.wasRecoveringFromReserve,
		inputs.money,
		inputs.reserveCash,
		0,  // blackMarketsInProgress - not needed for production decisions
		0   // supplyStashesInProgress - not needed for production decisions
	});
}

bool AIControlAdapterProductionManager::ShouldHoldArmyCap(const ProductionManagerInputs& inputs, int armyCap)
{
	const bool shouldPause = ShouldPauseCombatProduction(inputs);

	// Use existing policy function
	return AIControlAdapterShouldHoldArmyCap({
		shouldPause,
		inputs.isBalancedSprawl,
		inputs.wasArmyCapReached,
		inputs.armyCount,
		armyCap
	});
}

const char* AIControlAdapterProductionManager::ChoosePreferredUnit(const ProductionManagerInputs& inputs, int armyCap)
{
	// Use existing policy function
	const AIControlAdapterProductionChoiceResult result = AIControlAdapterChoosePreferredProductionCommand({
		ShouldPauseCombatProduction(inputs),
		nullptr,  // pauseReason - not needed here
		ShouldHoldArmyCap(inputs, armyCap),
		inputs.isBalancedSprawl,
		inputs.profile,
		inputs.money,
		inputs.barracks,
		inputs.armsDealers,
		inputs.palaces,
		inputs.hasCompletedPalace,
		inputs.hasScudLauncherScience,
		inputs.hasCaptureUpgrade,
		inputs.captureSources,
		inputs.captureSourcesLive,
		inputs.captureSourcesReserved,
		inputs.captureSourcesAvailable,
		inputs.capturableTargetsRemaining,
		inputs.desiredCaptureSources,
		inputs.maxCaptureConcurrent,
		inputs.soldiers,
		inputs.rpg,
		inputs.quads,
		inputs.scorpions,
		inputs.scudLaunchers,
		inputs.radarVans,
		inputs.armyCount,
		armyCap
	});

	return result.command;
}

bool AIControlAdapterProductionManager::SelectZonePreferredProducer(
	const ProductionManagerInputs& inputs,
	bool wantBarracks,
	ProductionIntent& outIntent)
{
	if (!inputs.hasActiveZone || inputs.ownedProducers == nullptr)
	{
		outIntent.reason = "no_zone_producer";
		return false;
	}

	const float zoneRadiusSq = std::max<float>(160.0f, inputs.zoneRadius) * std::max<float>(160.0f, inputs.zoneRadius);
	std::vector<const ProductionProducerSnapshot*> candidates;

	// Find eligible producers in active zone
	for (std::size_t i = 0; i < inputs.ownedProducers->size(); ++i)
	{
		const ProductionProducerSnapshot& producer = (*inputs.ownedProducers)[i];

		if (!producer.isStructure || producer.object == nullptr || producer.underConstruction)
		{
			continue;
		}

		// Check producer type
		if (wantBarracks)
		{
			if (!producer.isBarracks)
			{
				continue;
			}
		}
		else
		{
			if (!producer.isWarFactoryLike)
			{
				continue;
			}
		}

		// Check if in zone
		const float dx = producer.positionX - inputs.activeZoneCenterX;
		const float dy = producer.positionY - inputs.activeZoneCenterY;
		if ((dx * dx) + (dy * dy) > zoneRadiusSq)
		{
			continue;
		}

		candidates.push_back(&producer);
	}

	if (candidates.empty())
	{
		outIntent.reason = "no_zone_producer";
		return false;
	}

	// Sort by distance to zone center (nearest first)
	std::sort(candidates.begin(), candidates.end(), [&](const ProductionProducerSnapshot* lhs, const ProductionProducerSnapshot* rhs) -> bool
	{
		const float ldx = lhs->positionX - inputs.activeZoneCenterX;
		const float ldy = lhs->positionY - inputs.activeZoneCenterY;
		const float rdx = rhs->positionX - inputs.activeZoneCenterX;
		const float rdy = rhs->positionY - inputs.activeZoneCenterY;
		return (ldx * ldx) + (ldy * ldy) < (rdx * rdx) + (rdy * rdy);
	});

	// Select nearest producer
	outIntent.unitTemplate = "";  // Adapter will infer template when executing
	outIntent.producerKind = wantBarracks ? "barracks" : "arms_dealer";
	outIntent.producerObjectId = candidates[0]->objectId;
	outIntent.shouldProduce = true;
	return true;
}

ProductionIntent AIControlAdapterProductionManager::ChooseProduction(
	const ProductionManagerInputs& inputs,
	unsigned int currentTick)
{
	ProductionIntent intent;

	// Step 1: Calculate effective army cap
	const int armyCap = CalculateEffectiveArmyCap(inputs);

	// Step 2: Check if production should be paused
	const bool shouldPause = ShouldPauseCombatProduction(inputs);
	if (shouldPause)
	{
		const char* pauseReason = inputs.openingInfrastructureReady ? "reserve_cash_recovery" : "opening_not_ready";
		intent.reason = pauseReason;
		intent.shouldProduce = false;
		return intent;
	}

	// Step 3: Check if army cap reached
	const bool shouldHold = ShouldHoldArmyCap(inputs, armyCap);
	if (shouldHold)
	{
		intent.reason = "army_cap_reached";
		intent.shouldProduce = false;
		return intent;
	}

	// Step 4: Select preferred unit
	const char* preferredCommand = ChoosePreferredUnit(inputs, armyCap);
	if (preferredCommand == nullptr || *preferredCommand == '\0')
	{
		intent.reason = "no_unit_chosen";
		intent.shouldProduce = false;
		return intent;
	}

	intent.commandName = preferredCommand;

	// Step 5: Determine producer selection strategy
	// For balanced sprawl without surplus pressure, prefer zone-local production
	// Otherwise, use "all" commands for maximum producer utilization
	bool useZoneProducer = inputs.isBalancedSprawl && !inputs.surplusProductionPressure && inputs.hasActiveZone;

	if (useZoneProducer)
	{
		// Determine producer type from command (only for barracks/arms dealer commands)
		bool wantBarracks = false;
		bool isZoneEligibleCommand = false;

		if (std::strcmp(preferredCommand, "Game.QueueRpgTroopersAllBarracks") == 0 ||
			std::strcmp(preferredCommand, "Game.QueueSoldiersAllBarracks") == 0)
		{
			wantBarracks = true;
			isZoneEligibleCommand = true;
		}
		else if (std::strcmp(preferredCommand, "Game.QueueQuadsAllWarFactories") == 0 ||
				 std::strcmp(preferredCommand, "Game.QueueScorpionsAllWarFactories") == 0)
		{
			wantBarracks = false;
			isZoneEligibleCommand = true;
		}

		// Only try zone production for commands that use barracks/arms dealers
		if (isZoneEligibleCommand && SelectZonePreferredProducer(inputs, wantBarracks, intent))
		{
			// Successfully selected zone-preferred producer
			intent.shouldProduce = true;
			return intent;
		}
	}

	// Step 6: Fallback to "all" command (uses all eligible producers)
	intent.unitTemplate = "";  // Not needed for "all" commands
	intent.producerKind = "any";
	intent.producerObjectId = -1;
	intent.shouldProduce = true;
	intent.reason = "";
	return intent;
}
