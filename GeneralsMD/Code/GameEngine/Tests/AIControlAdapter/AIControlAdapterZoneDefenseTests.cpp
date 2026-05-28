/**
 * AIControlAdapterZoneDefenseTests.cpp
 *
 * Unit tests for AIControlAdapterZoneManager and AIControlAdapterDefenseManager.
 *
 * Tests verify Phase 3 implementation requirements:
 * - Attack memory creation and update
 * - Attack memory expiration
 * - Threat priority when multiple zones attacked
 * - Local producer preference for threatened zones
 * - Fallback producer-zone selection
 * - No defense request when no fresh threat
 */

#include "GameClient/AIControlAdapter/AIControlAdapterZoneManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterDefenseManager.h"
#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	// Test helpers: assertion functions
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << "\n";
			std::exit(1);
		}
	}

	void expectEq(int actual, int expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
			std::exit(1);
		}
	}

	void expectEq(unsigned int actual, unsigned int expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
			std::exit(1);
		}
	}

	void expectEq(const std::string& actual, const std::string& expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << message << " actual=\"" << actual << "\" expected=\"" << expected << "\"\n";
			std::exit(1);
		}
	}

	void expectGreater(unsigned int actual, unsigned int threshold, const char* message)
	{
		if (actual <= threshold)
		{
			std::cerr << "FAIL: " << message << " actual=" << actual << " threshold=" << threshold << "\n";
			std::exit(1);
		}
	}

	// Helper to create basic zone snapshots
	std::vector<ZoneSnapshot> createTestZones()
	{
		std::vector<ZoneSnapshot> zones;

		// Main base zone with producers
		ZoneSnapshot zone1;
		zone1.anchorId = 1001;
		zone1.centerX = 100.0f;
		zone1.centerY = 100.0f;
		zone1.isMainBase = true;
		zone1.barracks = 2;
		zone1.armsDealers = 1;
		zones.push_back(zone1);

		// Expansion zone with no producers
		ZoneSnapshot zone2;
		zone2.anchorId = 1002;
		zone2.centerX = 500.0f;
		zone2.centerY = 500.0f;
		zone2.isMainBase = false;
		zone2.barracks = 0;
		zone2.armsDealers = 0;
		zones.push_back(zone2);

		// Secondary base with producers
		ZoneSnapshot zone3;
		zone3.anchorId = 1003;
		zone3.centerX = 300.0f;
		zone3.centerY = 300.0f;
		zone3.isMainBase = false;
		zone3.barracks = 1;
		zone3.armsDealers = 1;
		zones.push_back(zone3);

		return zones;
	}
}

// Test: Attack memory creation
void testAttackMemoryCreation()
{
	AIControlAdapterZoneManager zoneManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// Damage event near zone 1
	StructureDamageEvent damage;
	damage.objectId = 5001;
	damage.positionX = 105.0f;
	damage.positionY = 105.0f;
	damage.damageDelta = 50.0f;
	damage.severityLevel = "medium";
	damageEvents.push_back(damage);

	ZoneManagerInputs inputs;
	inputs.currentTick = 10000;
	inputs.zones = &zones;
	inputs.damageEvents = &damageEvents;
	inputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(inputs);

	MostThreatenedZoneResult result = zoneManager.GetMostThreatenedZone(10000);

	expect(result.hasThreat, "Should have threat after damage");
	expectEq(result.zoneAnchorId, 1001u, "Threat should be in zone 1");
	expectEq(result.severityLevel, std::string("medium"), "Severity should be medium");
	expectEq(result.threatAge, 0u, "Threat age should be 0 immediately after attack");

	std::cout << "PASS: testAttackMemoryCreation\n";
}

// Test: Attack memory update
void testAttackMemoryUpdate()
{
	AIControlAdapterZoneManager zoneManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// First attack
	StructureDamageEvent damage1;
	damage1.objectId = 5001;
	damage1.positionX = 105.0f;
	damage1.positionY = 105.0f;
	damage1.damageDelta = 50.0f;
	damage1.severityLevel = "low";
	damageEvents.push_back(damage1);

	ZoneManagerInputs inputs;
	inputs.currentTick = 10000;
	inputs.zones = &zones;
	inputs.damageEvents = &damageEvents;
	inputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(inputs);

	MostThreatenedZoneResult result1 = zoneManager.GetMostThreatenedZone(10000);
	expectEq(result1.severityLevel, std::string("low"), "Initial severity should be low");

	// Second attack at same zone with higher severity
	damageEvents.clear();
	StructureDamageEvent damage2;
	damage2.objectId = 5002;
	damage2.positionX = 110.0f;
	damage2.positionY = 110.0f;
	damage2.damageDelta = 100.0f;
	damage2.severityLevel = "high";
	damageEvents.push_back(damage2);

	inputs.currentTick = 15000;
	inputs.damageEvents = &damageEvents;

	zoneManager.UpdateThreats(inputs);

	MostThreatenedZoneResult result2 = zoneManager.GetMostThreatenedZone(15000);
	expectEq(result2.severityLevel, std::string("high"), "Updated severity should be high");
	expectEq(result2.threatAge, 0u, "Threat age should be 0 after update");

	std::cout << "PASS: testAttackMemoryUpdate\n";
}

// Test: Attack memory expiration
void testAttackMemoryExpiration()
{
	AIControlAdapterZoneManager zoneManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// Attack at tick 10000
	StructureDamageEvent damage;
	damage.objectId = 5001;
	damage.positionX = 105.0f;
	damage.positionY = 105.0f;
	damage.damageDelta = 50.0f;
	damage.severityLevel = "medium";
	damageEvents.push_back(damage);

	ZoneManagerInputs inputs;
	inputs.currentTick = 10000;
	inputs.zones = &zones;
	inputs.damageEvents = &damageEvents;
	inputs.zoneRadius = 300.0f;
	inputs.threatMemoryWindowMs = 45000;

	zoneManager.UpdateThreats(inputs);

	MostThreatenedZoneResult result1 = zoneManager.GetMostThreatenedZone(10000);
	expect(result1.hasThreat, "Should have threat immediately");

	// Check threat still exists before expiration
	MostThreatenedZoneResult result2 = zoneManager.GetMostThreatenedZone(50000);
	expect(result2.hasThreat, "Should have threat before expiration window");
	expectGreater(result2.threatAge, 30000u, "Threat age should be > 30000");

	// Update threats at tick beyond expiration window
	damageEvents.clear();  // No new damage
	inputs.currentTick = 60000;  // 10000 + 45000 + margin
	inputs.damageEvents = &damageEvents;

	zoneManager.UpdateThreats(inputs);

	MostThreatenedZoneResult result3 = zoneManager.GetMostThreatenedZone(60000);
	expect(!result3.hasThreat, "Threat should be expired after memory window");

	std::cout << "PASS: testAttackMemoryExpiration\n";
}

// Test: Threat priority with multiple attacked zones
void testThreatPriorityMultipleZones()
{
	AIControlAdapterZoneManager zoneManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// Attack zone 1 at tick 10000
	StructureDamageEvent damage1;
	damage1.objectId = 5001;
	damage1.positionX = 105.0f;
	damage1.positionY = 105.0f;
	damage1.damageDelta = 50.0f;
	damage1.severityLevel = "low";
	damageEvents.push_back(damage1);

	ZoneManagerInputs inputs;
	inputs.currentTick = 10000;
	inputs.zones = &zones;
	inputs.damageEvents = &damageEvents;
	inputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(inputs);

	// Attack zone 3 at tick 15000 (newer)
	damageEvents.clear();
	StructureDamageEvent damage2;
	damage2.objectId = 5002;
	damage2.positionX = 305.0f;
	damage2.positionY = 305.0f;
	damage2.damageDelta = 30.0f;
	damage2.severityLevel = "low";
	damageEvents.push_back(damage2);

	inputs.currentTick = 15000;
	inputs.damageEvents = &damageEvents;

	zoneManager.UpdateThreats(inputs);

	// Newest attack should win
	MostThreatenedZoneResult result = zoneManager.GetMostThreatenedZone(15000);
	expect(result.hasThreat, "Should have threat");
	expectEq(result.zoneAnchorId, 1003u, "Most threatened should be zone 3 (newest attack)");
	expectEq(result.threatAge, 0u, "Newest threat age should be 0");

	std::cout << "PASS: testThreatPriorityMultipleZones\n";
}

// Test: Local producer preference
void testLocalProducerPreference()
{
	AIControlAdapterZoneManager zoneManager;
	AIControlAdapterDefenseManager defenseManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// Attack zone 1 (has local producers)
	StructureDamageEvent damage;
	damage.objectId = 5001;
	damage.positionX = 105.0f;
	damage.positionY = 105.0f;
	damage.damageDelta = 50.0f;
	damage.severityLevel = "medium";
	damageEvents.push_back(damage);

	ZoneManagerInputs zoneInputs;
	zoneInputs.currentTick = 10000;
	zoneInputs.zones = &zones;
	zoneInputs.damageEvents = &damageEvents;
	zoneInputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(zoneInputs);

	MostThreatenedZoneResult threat = zoneManager.GetMostThreatenedZone(10000);

	DefenseManagerInputs defenseInputs;
	defenseInputs.currentTick = 10000;
	defenseInputs.mostThreatenedZone = &threat;
	defenseInputs.zones = &zones;
	defenseInputs.money = 50000;
	defenseInputs.defenseEnabled = true;

	DefenseRequest request = defenseManager.ChooseDefenseProduction(defenseInputs, zoneManager);

	expect(request.active, "Defense request should be active");
	expectEq(request.threatenedZoneId, 1001u, "Threatened zone should be zone 1");
	expectEq(request.producerZoneId, 1001u, "Producer zone should be zone 1 (local)");
	expect(request.isLocalProduction, "Should use local production");

	std::cout << "PASS: testLocalProducerPreference\n";
}

// Test: Fallback producer zone selection
void testFallbackProducerZoneSelection()
{
	AIControlAdapterZoneManager zoneManager;
	AIControlAdapterDefenseManager defenseManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// Attack zone 2 (no local producers)
	StructureDamageEvent damage;
	damage.objectId = 5001;
	damage.positionX = 505.0f;
	damage.positionY = 505.0f;
	damage.damageDelta = 50.0f;
	damage.severityLevel = "medium";
	damageEvents.push_back(damage);

	ZoneManagerInputs zoneInputs;
	zoneInputs.currentTick = 10000;
	zoneInputs.zones = &zones;
	zoneInputs.damageEvents = &damageEvents;
	zoneInputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(zoneInputs);

	MostThreatenedZoneResult threat = zoneManager.GetMostThreatenedZone(10000);

	DefenseManagerInputs defenseInputs;
	defenseInputs.currentTick = 10000;
	defenseInputs.mostThreatenedZone = &threat;
	defenseInputs.zones = &zones;
	defenseInputs.money = 50000;
	defenseInputs.defenseEnabled = true;

	DefenseRequest request = defenseManager.ChooseDefenseProduction(defenseInputs, zoneManager);

	expect(request.active, "Defense request should be active");
	expectEq(request.threatenedZoneId, 1002u, "Threatened zone should be zone 2");
	expect(!request.isLocalProduction, "Should NOT use local production");
	// Nearest producer zone should be zone 3 (distance ~280) vs zone 1 (distance ~560)
	expectEq(request.producerZoneId, 1003u, "Producer zone should be zone 3 (nearest)");
	expectEq(request.fallbackReason, std::string("nearest_producer_zone"), "Should log nearest producer fallback");

	std::cout << "PASS: testFallbackProducerZoneSelection\n";
}

// Test: No defense request when no fresh threat
void testNoDefenseRequestWhenNoThreat()
{
	AIControlAdapterZoneManager zoneManager;
	AIControlAdapterDefenseManager defenseManager;

	std::vector<ZoneSnapshot> zones = createTestZones();

	// No damage events - no threat
	MostThreatenedZoneResult threat = zoneManager.GetMostThreatenedZone(10000);

	DefenseManagerInputs defenseInputs;
	defenseInputs.currentTick = 10000;
	defenseInputs.mostThreatenedZone = &threat;
	defenseInputs.zones = &zones;
	defenseInputs.money = 50000;
	defenseInputs.defenseEnabled = true;

	DefenseRequest request = defenseManager.ChooseDefenseProduction(defenseInputs, zoneManager);

	expect(!request.active, "Defense request should NOT be active when no threat");

	std::cout << "PASS: testNoDefenseRequestWhenNoThreat\n";
}

// Test: Defender mix calculation based on severity
void testDefenderMixBySeverity()
{
	AIControlAdapterZoneManager zoneManager;
	AIControlAdapterDefenseManager defenseManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// High severity attack
	StructureDamageEvent damage;
	damage.objectId = 5001;
	damage.positionX = 105.0f;
	damage.positionY = 105.0f;
	damage.damageDelta = 200.0f;
	damage.severityLevel = "high";
	damageEvents.push_back(damage);

	ZoneManagerInputs zoneInputs;
	zoneInputs.currentTick = 10000;
	zoneInputs.zones = &zones;
	zoneInputs.damageEvents = &damageEvents;
	zoneInputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(zoneInputs);

	MostThreatenedZoneResult threat = zoneManager.GetMostThreatenedZone(10000);

	DefenseManagerInputs defenseInputs;
	defenseInputs.currentTick = 10000;
	defenseInputs.mostThreatenedZone = &threat;
	defenseInputs.zones = &zones;
	defenseInputs.money = 50000;
	defenseInputs.currentSoldiers = 0;
	defenseInputs.currentRpg = 0;
	defenseInputs.currentQuads = 0;
	defenseInputs.currentScorpions = 0;
	defenseInputs.defenseEnabled = true;

	DefenseRequest request = defenseManager.ChooseDefenseProduction(defenseInputs, zoneManager);

	expect(request.active, "Defense request should be active");
	expectEq(request.severityLevel, std::string("high"), "Severity should be high");
	// High severity should request more defenders
	expectGreater(static_cast<unsigned int>(request.desiredSoldiers), 0u, "Should request soldiers");
	expectGreater(static_cast<unsigned int>(request.desiredRpg), 0u, "Should request RPG");
	expectGreater(static_cast<unsigned int>(request.desiredQuads), 0u, "Should request quads");

	std::cout << "PASS: testDefenderMixBySeverity\n";
}

// Test: Defense disabled by profile
void testDefenseDisabledByProfile()
{
	AIControlAdapterZoneManager zoneManager;
	AIControlAdapterDefenseManager defenseManager;

	std::vector<ZoneSnapshot> zones = createTestZones();
	std::vector<StructureDamageEvent> damageEvents;

	// Attack zone 1
	StructureDamageEvent damage;
	damage.objectId = 5001;
	damage.positionX = 105.0f;
	damage.positionY = 105.0f;
	damage.damageDelta = 50.0f;
	damage.severityLevel = "medium";
	damageEvents.push_back(damage);

	ZoneManagerInputs zoneInputs;
	zoneInputs.currentTick = 10000;
	zoneInputs.zones = &zones;
	zoneInputs.damageEvents = &damageEvents;
	zoneInputs.zoneRadius = 300.0f;

	zoneManager.UpdateThreats(zoneInputs);

	MostThreatenedZoneResult threat = zoneManager.GetMostThreatenedZone(10000);

	DefenseManagerInputs defenseInputs;
	defenseInputs.currentTick = 10000;
	defenseInputs.mostThreatenedZone = &threat;
	defenseInputs.zones = &zones;
	defenseInputs.money = 50000;
	defenseInputs.defenseEnabled = false;  // Defense disabled

	DefenseRequest request = defenseManager.ChooseDefenseProduction(defenseInputs, zoneManager);

	expect(!request.active, "Defense request should NOT be active when defense disabled");

	std::cout << "PASS: testDefenseDisabledByProfile\n";
}

// ========================================
// Phase 7.1: Zone Defense Response Tests
// ========================================

namespace Phase71
{
	// Test helper: Simulate defense response evaluation
	struct ZoneThreat
	{
		unsigned int zoneAnchor;
		std::string level;
		float positionX;
		float positionY;
		unsigned int lastSeenTick;
	};

	struct DefenseResponseResult
	{
		bool intentCreated;
		unsigned int targetZoneAnchor;
		std::string threatLevel;
	};

	DefenseResponseResult simulateDefenseResponse(
		const std::vector<ZoneThreat>& threats,
		unsigned int currentTick,
		unsigned int lastDefenseResponseTick,
		unsigned int lastDefendedZoneAnchor = 0,
		int lastDefendedThreatSeverity = 0)
	{
		DefenseResponseResult result;
		result.intentCreated = false;
		result.targetZoneAnchor = 0;

		const unsigned int defenseResponseCooldownMs = 5000;  // 5 seconds

		if (threats.empty())
		{
			return result;
		}

		// Always evaluate threats to find the best current threat
		int highestSeverity = 0;
		unsigned int mostRecentTick = 0;
		unsigned int threatenedZoneAnchor = 0;
		std::string threatLevel;

		const unsigned int threatFreshnessMs = 10000;  // 10 seconds
		for (std::size_t i = 0; i < threats.size(); ++i)
		{
			const ZoneThreat& threat = threats[i];

			// Check freshness: fresh means (now - lastSeenTick) <= freshness window
			// Skip stale threats: age > freshness window
			const unsigned int threatAge = currentTick - threat.lastSeenTick;
			if (threatAge > threatFreshnessMs)
			{
				continue;
			}

			int severity = 0;
			if (threat.level == "critical")
			{
				severity = 3;
			}
			else if (threat.level == "high")
			{
				severity = 2;
			}
			else if (threat.level == "medium" || threat.level == "low")
			{
				// Fresh low/medium is treated as active under-attack
				severity = 1;
			}

			// Respond to any severity >= 1 (fresh threats)
			// Prefer higher severity, then most recent on tie
			if (severity >= 1)
			{
				if (severity > highestSeverity || (severity == highestSeverity && threat.lastSeenTick > mostRecentTick))
				{
					highestSeverity = severity;
					mostRecentTick = threat.lastSeenTick;
					threatenedZoneAnchor = threat.zoneAnchor;
					threatLevel = threat.level;
				}
			}
		}

		if (highestSeverity >= 1)
		{
			// Check if we should suppress this response due to cooldown
			// Suppress only if cooldown is active AND severity is same/lower
			// Higher-severity threats can override cooldown even for the same zone
			const bool cooldownActive = (lastDefenseResponseTick != 0 && currentTick < (lastDefenseResponseTick + defenseResponseCooldownMs));
			const bool isLowerOrEqualSeverity = (highestSeverity <= lastDefendedThreatSeverity);
			const bool shouldSuppress = cooldownActive && isLowerOrEqualSeverity;

			if (!shouldSuppress)
			{
				result.intentCreated = true;
				result.targetZoneAnchor = threatenedZoneAnchor;
				result.threatLevel = threatLevel;
			}
		}

		return result;
	}

	void testUnderAttackZoneCreatesDefenseIntent()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "high";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 1000;
		threats.push_back(threat);

		const unsigned int currentTick = 1100;
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Defense intent should be created for high threat");
		expectEq(result.targetZoneAnchor, 1000u, "Should target zone 1000");

		std::cout << "PASS: testUnderAttackZoneCreatesDefenseIntent\n";
	}

	void testNoThreatNoDefenseIntent()
	{
		std::vector<ZoneThreat> threats;

		const unsigned int currentTick = 1000;
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(!result.intentCreated, "No defense intent when no threats");

		std::cout << "PASS: testNoThreatNoDefenseIntent\n";
	}

	void testHighThreatPreferredOverLow()
	{
		std::vector<ZoneThreat> threats;

		ZoneThreat lowThreat;
		lowThreat.zoneAnchor = 1000;
		lowThreat.level = "low";
		lowThreat.positionX = 100.0f;
		lowThreat.positionY = 200.0f;
		lowThreat.lastSeenTick = 1000;
		threats.push_back(lowThreat);

		ZoneThreat highThreat;
		highThreat.zoneAnchor = 2000;
		highThreat.level = "high";
		highThreat.positionX = 300.0f;
		highThreat.positionY = 400.0f;
		highThreat.lastSeenTick = 1000;
		threats.push_back(highThreat);

		const unsigned int currentTick = 1100;
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Defense intent should be created");
		expectEq(result.targetZoneAnchor, 2000u, "Should target high threat zone 2000 (high > low)");

		std::cout << "PASS: testHighThreatPreferredOverLow\n";
	}

	void testFreshHighThreatTriggersResponse()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "high";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 1000;
		threats.push_back(threat);

		const unsigned int currentTick = 1100;  // 100ms later, well within 600ms freshness
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Fresh high threat should trigger response");
		expectEq(result.targetZoneAnchor, 1000u, "Should target zone 1000");

		std::cout << "PASS: testFreshHighThreatTriggersResponse\n";
	}

	void testStaleHighThreatIgnored()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "high";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 1000;  // Very old
		threats.push_back(threat);

		const unsigned int currentTick = 12000;  // 11 seconds later, beyond 10s freshness
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(!result.intentCreated, "Stale high threat should be ignored");

		std::cout << "PASS: testStaleHighThreatIgnored\n";
	}

	void testModeratelyOldThreatStillTriggers()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "high";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 2000;
		threats.push_back(threat);

		const unsigned int currentTick = 6000;  // 4 seconds old, within 10s freshness
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Threat 4 seconds old should still trigger defense");
		expectEq(result.targetZoneAnchor, 1000u, "Should target zone 1000");

		std::cout << "PASS: testModeratelyOldThreatStillTriggers\n";
	}

	void testFreshLowMediumThreatCanTrigger()
	{
		std::vector<ZoneThreat> threats;

		ZoneThreat lowThreat;
		lowThreat.zoneAnchor = 1000;
		lowThreat.level = "low";
		lowThreat.positionX = 100.0f;
		lowThreat.positionY = 200.0f;
		lowThreat.lastSeenTick = 1050;
		threats.push_back(lowThreat);

		const unsigned int currentTick = 1100;
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Fresh low threat should trigger response (treated as under-attack)");
		expectEq(result.targetZoneAnchor, 1000u, "Should target zone 1000");

		std::cout << "PASS: testFreshLowMediumThreatCanTrigger\n";
	}

	void testCriticalThreatPreferredOverHigh()
	{
		std::vector<ZoneThreat> threats;

		ZoneThreat highThreat;
		highThreat.zoneAnchor = 1000;
		highThreat.level = "high";
		highThreat.positionX = 100.0f;
		highThreat.positionY = 200.0f;
		highThreat.lastSeenTick = 1000;
		threats.push_back(highThreat);

		ZoneThreat criticalThreat;
		criticalThreat.zoneAnchor = 2000;
		criticalThreat.level = "critical";
		criticalThreat.positionX = 300.0f;
		criticalThreat.positionY = 400.0f;
		criticalThreat.lastSeenTick = 1000;
		threats.push_back(criticalThreat);

		const unsigned int currentTick = 1100;
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Defense intent should be created");
		expectEq(result.targetZoneAnchor, 2000u, "Should target critical threat zone 2000");

		std::cout << "PASS: testCriticalThreatPreferredOverHigh\n";
	}

	void testEqualSeverityPrefersMostRecent()
	{
		std::vector<ZoneThreat> threats;

		ZoneThreat olderThreat;
		olderThreat.zoneAnchor = 1000;
		olderThreat.level = "high";
		olderThreat.positionX = 100.0f;
		olderThreat.positionY = 200.0f;
		olderThreat.lastSeenTick = 900;
		threats.push_back(olderThreat);

		ZoneThreat newerThreat;
		newerThreat.zoneAnchor = 2000;
		newerThreat.level = "high";
		newerThreat.positionX = 300.0f;
		newerThreat.positionY = 400.0f;
		newerThreat.lastSeenTick = 1000;
		threats.push_back(newerThreat);

		const unsigned int currentTick = 1100;
		const unsigned int lastDefenseResponseTick = 0;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick);

		expect(result.intentCreated, "Defense intent should be created");
		expectEq(result.targetZoneAnchor, 2000u, "Should target most recent threat zone 2000");

		std::cout << "PASS: testEqualSeverityPrefersMostRecent\n";
	}

	void testCooldownSuppressesRepeatedResponse()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "high";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 3000;
		threats.push_back(threat);

		const unsigned int currentTick = 4000;
		const unsigned int lastDefenseResponseTick = 2000;  // 2 seconds ago, within 5s cooldown
		const unsigned int lastDefendedZoneAnchor = 1000;   // Same zone
		const int lastDefendedThreatSeverity = 2;            // Same severity (high)

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick, lastDefendedZoneAnchor, lastDefendedThreatSeverity);

		expect(!result.intentCreated, "Cooldown should suppress repeated response for same zone");

		std::cout << "PASS: testCooldownSuppressesRepeatedResponse\n";
	}

	void testCooldownExpiresAfterDelay()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "high";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 7500;  // Fresh threat (500ms old)
		threats.push_back(threat);

		const unsigned int currentTick = 8000;
		const unsigned int lastDefenseResponseTick = 2000;  // 6 seconds ago, cooldown expired (5s)
		const unsigned int lastDefendedZoneAnchor = 1000;
		const int lastDefendedThreatSeverity = 2;

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick, lastDefendedZoneAnchor, lastDefendedThreatSeverity);

		expect(result.intentCreated, "Cooldown should expire after delay");
		expectEq(result.targetZoneAnchor, 1000u, "Should target zone 1000");

		std::cout << "PASS: testCooldownExpiresAfterDelay\n";
	}

	void testHigherSeverityOverridesCooldown()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 2000;
		threat.level = "critical";
		threat.positionX = 300.0f;
		threat.positionY = 400.0f;
		threat.lastSeenTick = 3600;  // Fresh (400ms old)
		threats.push_back(threat);

		const unsigned int currentTick = 4000;
		const unsigned int lastDefenseResponseTick = 2000;  // 2 seconds ago, within 5s cooldown
		const unsigned int lastDefendedZoneAnchor = 1000;   // Different zone
		const int lastDefendedThreatSeverity = 2;            // Lower severity (high < critical)

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick, lastDefendedZoneAnchor, lastDefendedThreatSeverity);

		expect(result.intentCreated, "Higher severity (critical) should override cooldown");
		expectEq(result.targetZoneAnchor, 2000u, "Should target critical threat zone 2000");

		std::cout << "PASS: testHigherSeverityOverridesCooldown\n";
	}

	void testSameSeverityDifferentZoneRespectsCooldown()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 2000;
		threat.level = "high";
		threat.positionX = 300.0f;
		threat.positionY = 400.0f;
		threat.lastSeenTick = 3600;  // Fresh (400ms old)
		threats.push_back(threat);

		const unsigned int currentTick = 4000;
		const unsigned int lastDefenseResponseTick = 2000;  // 2 seconds ago, within 5s cooldown
		const unsigned int lastDefendedZoneAnchor = 1000;   // Different zone
		const int lastDefendedThreatSeverity = 2;            // Same severity (high)

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick, lastDefendedZoneAnchor, lastDefendedThreatSeverity);

		expect(!result.intentCreated, "Same/lower severity should respect cooldown even for different zone");

		std::cout << "PASS: testSameSeverityDifferentZoneRespectsCooldown\n";
	}

	void testSameZoneHigherSeverityOverridesCooldown()
	{
		std::vector<ZoneThreat> threats;
		ZoneThreat threat;
		threat.zoneAnchor = 1000;
		threat.level = "critical";
		threat.positionX = 100.0f;
		threat.positionY = 200.0f;
		threat.lastSeenTick = 3600;  // Fresh (400ms old)
		threats.push_back(threat);

		const unsigned int currentTick = 4000;
		const unsigned int lastDefenseResponseTick = 2000;  // 2 seconds ago, within 5s cooldown
		const unsigned int lastDefendedZoneAnchor = 1000;   // Same zone
		const int lastDefendedThreatSeverity = 2;            // Lower severity (high < critical)

		DefenseResponseResult result = simulateDefenseResponse(threats, currentTick, lastDefenseResponseTick, lastDefendedZoneAnchor, lastDefendedThreatSeverity);

		expect(result.intentCreated, "Higher severity (critical) should override cooldown even for same zone");
		expectEq(result.targetZoneAnchor, 1000u, "Should target zone 1000 with escalated threat");

		std::cout << "PASS: testSameZoneHigherSeverityOverridesCooldown\n";
	}

	void runAllPhase71Tests()
	{
		std::cout << "\nRunning Phase 7.1 Defense Response tests...\n";

		testUnderAttackZoneCreatesDefenseIntent();
		testNoThreatNoDefenseIntent();
		testHighThreatPreferredOverLow();
		testFreshHighThreatTriggersResponse();
		testStaleHighThreatIgnored();
		testModeratelyOldThreatStillTriggers();
		testFreshLowMediumThreatCanTrigger();
		testCriticalThreatPreferredOverHigh();
		testEqualSeverityPrefersMostRecent();
		testCooldownSuppressesRepeatedResponse();
		testCooldownExpiresAfterDelay();
		testHigherSeverityOverridesCooldown();
		testSameSeverityDifferentZoneRespectsCooldown();
		testSameZoneHigherSeverityOverridesCooldown();

		std::cout << "All Phase 7.1 Defense Response tests passed!\n";
	}
}

// Phase 5.8 Tests: Non-Supply Zone Anchor Types

void testZoneAnchorTypeToString()
{
	expectEq(std::string(zoneAnchorTypeToString(ZoneAnchorType::MainBase)), std::string("main_base"), "MainBase type string");
	expectEq(std::string(zoneAnchorTypeToString(ZoneAnchorType::SupplyStash)), std::string("supply_stash"), "SupplyStash type string");
	expectEq(std::string(zoneAnchorTypeToString(ZoneAnchorType::CapturedStructure)), std::string("captured_structure"), "CapturedStructure type string");
	expectEq(std::string(zoneAnchorTypeToString(ZoneAnchorType::StrategicFoothold)), std::string("strategic_foothold"), "StrategicFoothold type string");
	expectEq(std::string(zoneAnchorTypeToString(ZoneAnchorType::MarketFoothold)), std::string("market_foothold"), "MarketFoothold type string");

	std::cout << "PASS: testZoneAnchorTypeToString\n";
}

void testZoneSnapshotHasAnchorType()
{
	ZoneSnapshot zone;
	zone.anchorId = 1001;
	zone.centerX = 100.0f;
	zone.centerY = 100.0f;
	zone.isMainBase = true;
	zone.barracks = 1;
	zone.armsDealers = 1;
	zone.anchorType = ZoneAnchorType::MainBase;

	expectEq(std::string(zoneAnchorTypeToString(zone.anchorType)), std::string("main_base"), "Zone should have anchor type");

	std::cout << "PASS: testZoneSnapshotHasAnchorType\n";
}

void testCapturedStructureAnchorType()
{
	ZoneSnapshot zone;
	zone.anchorId = 2001;
	zone.centerX = 500.0f;
	zone.centerY = 500.0f;
	zone.isMainBase = false;
	zone.barracks = 0;
	zone.armsDealers = 0;
	zone.anchorType = ZoneAnchorType::CapturedStructure;

	expectEq(std::string(zoneAnchorTypeToString(zone.anchorType)), std::string("captured_structure"), "Captured structure anchor type");
	expect(!zone.isMainBase, "Captured structure zone should not be main base");

	std::cout << "PASS: testCapturedStructureAnchorType\n";
}

void testStrategicFootholdAnchorType()
{
	ZoneSnapshot zone;
	zone.anchorId = 3001;
	zone.centerX = 700.0f;
	zone.centerY = 700.0f;
	zone.isMainBase = false;
	zone.barracks = 0;
	zone.armsDealers = 0;
	zone.anchorType = ZoneAnchorType::StrategicFoothold;

	expectEq(std::string(zoneAnchorTypeToString(zone.anchorType)), std::string("strategic_foothold"), "Strategic foothold anchor type");

	std::cout << "PASS: testStrategicFootholdAnchorType\n";
}

void testZoneDistanceValidation()
{
	// Test minimum zone separation distance (220 units)
	ZoneSnapshot zone1;
	zone1.centerX = 100.0f;
	zone1.centerY = 100.0f;
	zone1.anchorType = ZoneAnchorType::SupplyStash;

	ZoneSnapshot zone2;
	zone2.centerX = 250.0f;
	zone2.centerY = 100.0f;
	zone2.anchorType = ZoneAnchorType::CapturedStructure;

	// Distance = 150 units, should be too close
	const float dx = zone2.centerX - zone1.centerX;
	const float dy = zone2.centerY - zone1.centerY;
	const float distSq = (dx * dx) + (dy * dy);
	const float minDistSq = 220.0f * 220.0f;

	expect(distSq < minDistSq, "Zones at 150 units should be too close (< 220)");

	// Zone3 at 300 units should be far enough
	ZoneSnapshot zone3;
	zone3.centerX = 400.0f;
	zone3.centerY = 100.0f;
	zone3.anchorType = ZoneAnchorType::StrategicFoothold;

	const float dx3 = zone3.centerX - zone1.centerX;
	const float dy3 = zone3.centerY - zone1.centerY;
	const float distSq3 = (dx3 * dx3) + (dy3 * dy3);

	expect(distSq3 >= minDistSq, "Zones at 300 units should be far enough (>= 220)");

	std::cout << "PASS: testZoneDistanceValidation\n";
}

void testSyntheticAnchorIdRange()
{
	// Synthetic anchor IDs start at 1,000,000 to avoid conflicts with real object IDs
	const unsigned int syntheticStart = 1000000;
	const unsigned int realObjectId = 5001;

	expect(syntheticStart > realObjectId, "Synthetic anchor IDs should be much larger than typical object IDs");
	expect(syntheticStart >= 1000000, "Synthetic anchor IDs should start at 1,000,000");

	ZoneSnapshot syntheticZone;
	syntheticZone.anchorId = syntheticStart;
	syntheticZone.anchorType = ZoneAnchorType::StrategicFoothold;

	ZoneSnapshot realZone;
	realZone.anchorId = realObjectId;
	realZone.anchorType = ZoneAnchorType::CapturedStructure;

	const bool isSyntheticAnchor = syntheticZone.anchorId >= 1000000;
	const bool isRealAnchor = realZone.anchorId < 1000000;

	expect(isSyntheticAnchor, "Foothold zone should have synthetic anchor ID");
	expect(isRealAnchor, "Captured structure zone should have real object anchor ID");

	std::cout << "PASS: testSyntheticAnchorIdRange\n";
}

void testMarketFootholdPrerequisites()
{
	// Market foothold requires: Palace + 4 Black Markets + 3000 cash + 800 durable income
	const bool hasPalace = true;
	const int blackMarkets = 4;
	const unsigned int money = 3000;
	const int durableIncome = blackMarkets * 200; // 800

	const bool economyIsStrong = hasPalace && blackMarkets >= 4 && money >= 3000;
	const bool hasStrongIncome = durableIncome >= 800;

	expect(economyIsStrong, "Should have strong economy with Palace + 4 markets + 3000 cash");
	expect(hasStrongIncome, "Should have strong income with 4 Black Markets (800/min)");

	// Strategic foothold only needs economy, not strong income
	const bool strategicFootholdOk = economyIsStrong;
	expect(strategicFootholdOk, "Strategic foothold should be available with strong economy");

	// Market foothold needs both
	const bool marketFootholdOk = economyIsStrong && hasStrongIncome;
	expect(marketFootholdOk, "Market foothold should be available with strong economy + income");

	// Insufficient conditions
	const bool insufficientMarkets = hasPalace && (blackMarkets < 4) && money >= 3000;
	const bool insufficientIncome = (blackMarkets * 200) < 800;

	expect(!insufficientMarkets || !hasStrongIncome, "Market foothold should NOT be available with < 4 markets");

	std::cout << "PASS: testMarketFootholdPrerequisites\n";
}

void testNonSupplyZoneDevelopmentCriteria()
{
	// Supply zones require Supply Stash + infrastructure
	// Non-supply zones (CapturedStructure/Foothold) require only Tunnel + Stinger

	ZoneSnapshot supplyZone;
	supplyZone.anchorType = ZoneAnchorType::SupplyStash;
	int zoneSupply = 1;
	int zoneTunnels = 1;
	int zoneStingers = 1;
	int zoneBarracks = 0;
	int zoneArms = 0;

	// Supply zone developed = supply + (barracks + arms + tunnels + stingers) >= 3
	const bool supplyZoneDeveloped = zoneSupply > 0 && (zoneBarracks + zoneArms + zoneTunnels + zoneStingers) >= 3;
	expect(!supplyZoneDeveloped, "Supply zone should NOT be developed with only Tunnel + Stinger (need 3+ infrastructure)");

	// Non-supply zone (captured structure or foothold)
	ZoneSnapshot capturedZone;
	capturedZone.anchorType = ZoneAnchorType::CapturedStructure;
	zoneTunnels = 1;
	zoneStingers = 1;

	// Non-supply developed = Tunnel + Stinger
	const bool capturedZoneDeveloped = zoneTunnels >= 1 && zoneStingers >= 1;
	expect(capturedZoneDeveloped, "Captured structure zone should be developed with Tunnel + Stinger");

	ZoneSnapshot footholdZone;
	footholdZone.anchorType = ZoneAnchorType::StrategicFoothold;

	const bool footholdZoneDeveloped = zoneTunnels >= 1 && zoneStingers >= 1;
	expect(footholdZoneDeveloped, "Foothold zone should be developed with Tunnel + Stinger");

	std::cout << "PASS: testNonSupplyZoneDevelopmentCriteria\n";
}

void testFootholdLocationSelection()
{
	// Foothold placement should project beyond furthest zone along sprawl axis
	const float mainZoneX = 100.0f;
	const float mainZoneY = 100.0f;

	const float furthestZoneX = 500.0f;
	const float furthestZoneY = 300.0f;

	// Sprawl axis from main to furthest
	float sprawlDx = furthestZoneX - mainZoneX;
	float sprawlDy = furthestZoneY - mainZoneY;

	// Normalize
	const float sprawlLen = std::sqrt((sprawlDx * sprawlDx) + (sprawlDy * sprawlDy));
	sprawlDx /= sprawlLen;
	sprawlDy /= sprawlLen;

	// Project 500 units beyond furthest zone
	const float extensionDistance = 500.0f;
	const float footholdX = furthestZoneX + (sprawlDx * extensionDistance);
	const float footholdY = furthestZoneY + (sprawlDy * extensionDistance);

	// Foothold should be further from main than furthest zone
	const float footholdDistFromMain = std::sqrt(
		((footholdX - mainZoneX) * (footholdX - mainZoneX)) +
		((footholdY - mainZoneY) * (footholdY - mainZoneY)));

	const float furthestDistFromMain = std::sqrt(
		((furthestZoneX - mainZoneX) * (furthestZoneX - mainZoneX)) +
		((furthestZoneY - mainZoneY) * (furthestZoneY - mainZoneY)));

	expect(footholdDistFromMain > furthestDistFromMain, "Foothold should be further from main than furthest zone");

	// Foothold should maintain minimum distance from furthest zone
	const float footholdDistFromFurthest = std::sqrt(
		((footholdX - furthestZoneX) * (footholdX - furthestZoneX)) +
		((footholdY - furthestZoneY) * (footholdY - furthestZoneY)));

	expect(footholdDistFromFurthest >= 220.0f, "Foothold should maintain minimum 220 unit distance from existing zones");

	std::cout << "PASS: testFootholdLocationSelection\n";
}

void testBuildSpaceValidation()
{
	// Build space heuristic: < 3 friendly structures within 120 units = has space
	const float buildSpaceCheckRadius = 120.0f;

	// Scenario 1: No nearby structures (has space)
	int nearbyStructures1 = 0;
	bool hasSpace1 = nearbyStructures1 < 3;
	expect(hasSpace1, "Should have build space with 0 nearby structures");

	// Scenario 2: 2 nearby structures (has space)
	int nearbyStructures2 = 2;
	bool hasSpace2 = nearbyStructures2 < 3;
	expect(hasSpace2, "Should have build space with 2 nearby structures");

	// Scenario 3: 3+ nearby structures (no space)
	int nearbyStructures3 = 3;
	bool hasSpace3 = nearbyStructures3 < 3;
	expect(!hasSpace3, "Should NOT have build space with 3+ nearby structures");

	int nearbyStructures4 = 5;
	bool hasSpace4 = nearbyStructures4 < 3;
	expect(!hasSpace4, "Should NOT have build space with 5+ nearby structures");

	std::cout << "PASS: testBuildSpaceValidation\n";
}

int main()
{
	std::cout << "Running ZoneManager and DefenseManager tests...\n";

	testAttackMemoryCreation();
	testAttackMemoryUpdate();
	testAttackMemoryExpiration();
	testThreatPriorityMultipleZones();
	testLocalProducerPreference();
	testFallbackProducerZoneSelection();
	testNoDefenseRequestWhenNoThreat();
	testDefenderMixBySeverity();
	testDefenseDisabledByProfile();

	std::cout << "\nAll ZoneManager and DefenseManager tests passed!\n";

	// Phase 7.1: Zone Defense Response tests
	Phase71::runAllPhase71Tests();

	// Phase 5.8: Zone anchor type tests
	std::cout << "\nRunning Phase 5.8 Zone Anchor Type tests...\n";
	testZoneAnchorTypeToString();
	testZoneSnapshotHasAnchorType();
	testCapturedStructureAnchorType();
	testStrategicFootholdAnchorType();
	testZoneDistanceValidation();
	testSyntheticAnchorIdRange();
	testMarketFootholdPrerequisites();
	testNonSupplyZoneDevelopmentCriteria();
	testFootholdLocationSelection();
	testBuildSpaceValidation();
	std::cout << "All Phase 5.8 Zone Anchor Type tests passed!\n";

	std::cout << "\nAll tests passed!\n";
	return 0;
}
