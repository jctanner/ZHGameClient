#pragma once

struct AIControlAdapterProductionPolicyInputs
{
	bool isBalancedSprawl;
	bool openingInfrastructureReady;
	bool openingEconomyReady;
	bool wasRecoveringFromReserve;
	unsigned int money;
	unsigned int reserveCash;
	int blackMarketsInProgress;
	int supplyStashesInProgress;
};

bool AIControlAdapterShouldPauseCombatProduction(const AIControlAdapterProductionPolicyInputs& inputs);

struct AIControlAdapterCombatProductionPolicyInputs
{
	bool shouldPauseForEconomy;
	bool isBalancedSprawl;
	bool wasArmyCapReached;
	int combatCount;
	int armyCap;
};

bool AIControlAdapterShouldHoldArmyCap(const AIControlAdapterCombatProductionPolicyInputs& inputs);

struct AIControlAdapterOpeningPolicyInputs
{
	int completedSupplyStashes;
	int completedBarracks;
	int completedArmsDealers;
};

const char* AIControlAdapterGetRequiredOpeningBuild(const AIControlAdapterOpeningPolicyInputs& inputs);

struct AIControlAdapterBlackMarketPolicyInputs
{
	bool palaceExists;
	bool isBalancedSprawl;
	unsigned int money;
	unsigned int reserveCash;
	int blackMarketsInProgress;
};

bool AIControlAdapterCanAttemptBlackMarket(const AIControlAdapterBlackMarketPolicyInputs& inputs);
bool AIControlAdapterIsSettlingSensitiveBuild(const char* commandName);
unsigned int AIControlAdapterGetBuildRetryDelayMs(const char* commandName, bool success, const char* reason);

struct AIControlAdapterEcoRecoveryPolicyInputs
{
	bool isBalancedSprawl;
	int totalSupplyStashes;
	int totalPalaces;
	int totalBlackMarkets;
	int desiredMarketCount;
	bool shouldThrottleExtraStashGrowth;
	bool canAttemptBlackMarket;
};

const char* AIControlAdapterGetEcoRecoveryBuild(const AIControlAdapterEcoRecoveryPolicyInputs& inputs);

bool AIControlAdapterShouldAbortSciencePlanForTick(const char* reason);
bool AIControlAdapterShouldAbortUpgradePlanForTick(const char* reason);
unsigned int AIControlAdapterGetTechRetryDelayMs(bool issued, const char* reason);

struct AIControlAdapterRadarVanPolicyInputs
{
	int armsDealers;
	int radarVans;
	int combatVehicles;
	int minRadarVans;
};

bool AIControlAdapterShouldQueueRadarVan(const AIControlAdapterRadarVanPolicyInputs& inputs);

bool AIControlAdapterHasTickElapsed(unsigned int deadline, unsigned int now);
bool AIControlAdapterIsTickInFuture(unsigned int deadline, unsigned int now);

struct AIControlAdapterProductionChoiceInputs
{
	bool shouldPauseForEconomy;
	const char* pauseReason;
	bool shouldHoldArmyCap;
	bool isBalancedSprawl;
	const char* profile;
	unsigned int money;
	int barracks;
	int armsDealers;
	int palaces;
	int soldiers;
	int rpg;
	int quads;
	int scorpions;
	int scudLaunchers;
};

struct AIControlAdapterProductionChoiceResult
{
	const char* command;
	const char* reason;
};

AIControlAdapterProductionChoiceResult AIControlAdapterChoosePreferredProductionCommand(
	const AIControlAdapterProductionChoiceInputs& inputs);

struct AIControlAdapterVehicleSustainPolicyInputs
{
	bool isBalancedSprawl;
	int armsDealers;
	int barracks;
	int quads;
	int scorpions;
	int scudLaunchers;
	int radarVans;
	int soldiers;
	int rpg;
};

bool AIControlAdapterShouldPreferVehicleReplenishment(const AIControlAdapterVehicleSustainPolicyInputs& inputs);

struct AIControlAdapterMacroCompletionPolicyInputs
{
	bool isSprawlStyle;
	bool shouldForceEcoRecovery;
	bool remoteZoneNeedsFollowup;
	int totalSupplyStashes;
	int supplyCap;
	int totalBarracks;
	int barracksCap;
	int totalArmsDealers;
	int armsCap;
	int totalBlackMarkets;
	int marketCap;
	int totalTunnels;
	int tunnelCap;
	int totalStingers;
	int stingerCap;
};

bool AIControlAdapterShouldTreatMacroAsComplete(const AIControlAdapterMacroCompletionPolicyInputs& inputs);

struct AIControlAdapterMarketGrowthPolicyInputs
{
	bool isBalancedSprawl;
	bool shouldForceEcoRecovery;
	bool shouldPrioritizeMarketGrowth;
	bool canAttemptBlackMarket;
	bool shouldPreserveReserve;
	bool canScaleMilitaryProduction;
	bool isSprawlStyle;
	int totalBlackMarkets;
	int desiredMarketCount;
	int totalBarracks;
	int barracksCap;
	int totalArmsDealers;
	int armsCap;
};

bool AIControlAdapterShouldPrioritizeMarketsOverProductionBuildings(
	const AIControlAdapterMarketGrowthPolicyInputs& inputs);

bool AIControlAdapterShouldAbortUpgradePlanForReason(const char* reason);
