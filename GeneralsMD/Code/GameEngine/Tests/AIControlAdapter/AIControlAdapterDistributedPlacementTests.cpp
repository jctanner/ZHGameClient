/**
 * AIControlAdapterDistributedPlacementTests.cpp
 *
 * Tests for Phase 5.7 distributed safe economy placement scoring logic.
 */

#include <cassert>
#include <cstdio>

// Simplified zone scoring simulation to verify Phase 5.7 concepts

struct TestZone
{
	bool isMainBase = false;
	bool isActive = false;
	bool under_attack = false;
	bool needs_followup = false;
	unsigned int blackMarkets = 0;
	unsigned int barracks = 0;
	unsigned int tunnels = 0;
};

static int ScoreZone(const TestZone& zone, unsigned int completedBlackMarkets, unsigned int mainBaseThreshold)
{
	int score = 0;

	// Main base bonus for first few markets
	if (zone.isMainBase && completedBlackMarkets < mainBaseThreshold)
	{
		score += 100;
	}
	else if (zone.isMainBase)
	{
		score += 30;
	}

	// Heavy penalty for active zone
	if (zone.isActive)
	{
		score -= 200;
	}

	// Heavy penalty for under_attack
	if (zone.under_attack)
	{
		score -= 300;
	}

	// Penalty for needs_followup
	if (zone.needs_followup)
	{
		score -= 100;
	}

	// Penalty per existing Black Market
	score -= static_cast<int>(zone.blackMarkets) * 40;

	// Bonus for developed zone
	if (zone.barracks > 0)
	{
		score += 50;
	}

	// Bonus for defensive structures
	if (zone.tunnels > 0)
	{
		score += 20;
	}

	return score;
}

// Test: First few markets prefer main base
static void TestEarlyMarketsPreferMainBase()
{
	TestZone mainBase;
	mainBase.isMainBase = true;
	mainBase.barracks = 1;

	TestZone rearZone;
	rearZone.barracks = 1;
	rearZone.blackMarkets = 0;

	const unsigned int mainBaseThreshold = 6u;

	// For first few markets (< 6), main base should score higher
	for (unsigned int marketCount = 0; marketCount < mainBaseThreshold; ++marketCount)
	{
		int mainScore = ScoreZone(mainBase, marketCount, mainBaseThreshold);
		int rearScore = ScoreZone(rearZone, marketCount, mainBaseThreshold);

		assert(mainScore > rearScore);
	}

	printf("PASS: TestEarlyMarketsPreferMainBase\n");
}

// Test: Later markets spread to rear zones
static void TestLaterMarketsSpreadToRearZones()
{
	TestZone mainBase;
	mainBase.isMainBase = true;
	mainBase.barracks = 1;
	mainBase.blackMarkets = 4; // Already has several markets

	TestZone rearZone;
	rearZone.barracks = 1;
	rearZone.blackMarkets = 0; // No markets yet

	const unsigned int mainBaseThreshold = 6u;
	const unsigned int completedMarkets = 8u; // After threshold

	int mainScore = ScoreZone(mainBase, completedMarkets, mainBaseThreshold);
	int rearScore = ScoreZone(rearZone, completedMarkets, mainBaseThreshold);

	// Rear zone with no markets should score higher than saturated main base
	assert(rearScore > mainScore);

	printf("PASS: TestLaterMarketsSpreadToRearZones\n");
}

// Test: Active zones are heavily penalized
static void TestActiveZonesAvoided()
{
	TestZone activeZone;
	activeZone.isActive = true;
	activeZone.barracks = 1;

	TestZone safeZone;
	safeZone.barracks = 1;

	const unsigned int mainBaseThreshold = 6u;
	const unsigned int completedMarkets = 3u;

	int activeScore = ScoreZone(activeZone, completedMarkets, mainBaseThreshold);
	int safeScore = ScoreZone(safeZone, completedMarkets, mainBaseThreshold);

	// Safe zone should score much higher than active zone
	assert(safeScore > activeScore);
	assert(activeScore < 0); // Active zone should have negative score

	printf("PASS: TestActiveZonesAvoided\n");
}

// Test: Threatened zones are avoided
static void TestThreatenedZonesAvoided()
{
	TestZone threatenedZone;
	threatenedZone.under_attack = true;
	threatenedZone.barracks = 1;

	TestZone safeZone;
	safeZone.barracks = 1;

	const unsigned int mainBaseThreshold = 6u;
	const unsigned int completedMarkets = 3u;

	int threatenedScore = ScoreZone(threatenedZone, completedMarkets, mainBaseThreshold);
	int safeScore = ScoreZone(safeZone, completedMarkets, mainBaseThreshold);

	// Safe zone should score much higher than threatened zone
	assert(safeScore > threatenedScore);
	assert(threatenedScore < 0); // Threatened zone should have very negative score

	printf("PASS: TestThreatenedZonesAvoided\n");
}

// Test: Market-heavy zones are deprioritized
static void TestMarketHeavyZonesDeprioritized()
{
	TestZone saturatedZone;
	saturatedZone.barracks = 1;
	saturatedZone.blackMarkets = 5; // Already has many markets

	TestZone emptyZone;
	emptyZone.barracks = 1;
	emptyZone.blackMarkets = 0;

	const unsigned int mainBaseThreshold = 6u;
	const unsigned int completedMarkets = 10u;

	int saturatedScore = ScoreZone(saturatedZone, completedMarkets, mainBaseThreshold);
	int emptyScore = ScoreZone(emptyZone, completedMarkets, mainBaseThreshold);

	// Empty zone should score higher than saturated zone
	assert(emptyScore > saturatedScore);

	// Penalty should be significant (40 per market)
	int expectedPenalty = 5 * 40;
	assert((emptyScore - saturatedScore) >= expectedPenalty - 10);

	printf("PASS: TestMarketHeavyZonesDeprioritized\n");
}

// Test: Developed zones score higher than undeveloped
static void TestDevelopedZonesPreferred()
{
	TestZone developedZone;
	developedZone.barracks = 1;
	developedZone.tunnels = 1;

	TestZone undevelopedZone;
	undevelopedZone.needs_followup = true;

	const unsigned int mainBaseThreshold = 6u;
	const unsigned int completedMarkets = 3u;

	int developedScore = ScoreZone(developedZone, completedMarkets, mainBaseThreshold);
	int undevelopedScore = ScoreZone(undevelopedZone, completedMarkets, mainBaseThreshold);

	// Developed zone should score much higher
	assert(developedScore > undevelopedScore);

	printf("PASS: TestDevelopedZonesPreferred\n");
}

// Test: Distribution balances markets across zones
static void TestDistributionBalance()
{
	TestZone zone1;
	zone1.barracks = 1;
	zone1.blackMarkets = 0;

	TestZone zone2;
	zone2.barracks = 1;
	zone2.blackMarkets = 0;

	TestZone zone3;
	zone3.barracks = 1;
	zone3.blackMarkets = 3; // Already saturated

	const unsigned int mainBaseThreshold = 6u;
	const unsigned int completedMarkets = 10u;

	int score1 = ScoreZone(zone1, completedMarkets, mainBaseThreshold);
	int score2 = ScoreZone(zone2, completedMarkets, mainBaseThreshold);
	int score3 = ScoreZone(zone3, completedMarkets, mainBaseThreshold);

	// Zones 1 and 2 should score equally (both empty)
	assert(score1 == score2);

	// Saturated zone 3 should score lower
	assert(score1 > score3);
	assert(score2 > score3);

	printf("PASS: TestDistributionBalance\n");
}

int main()
{
	TestEarlyMarketsPreferMainBase();
	TestLaterMarketsSpreadToRearZones();
	TestActiveZonesAvoided();
	TestThreatenedZonesAvoided();
	TestMarketHeavyZonesDeprioritized();
	TestDevelopedZonesPreferred();
	TestDistributionBalance();

	printf("All Distributed Placement tests passed.\n");
	return 0;
}
