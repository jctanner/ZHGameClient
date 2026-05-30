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

namespace Phase78
{
	void testTwoThreatsReceiveCappedAllocations()
	{
		const AIControlAdapterZoneDefenseBudgetResult highBudget = AIControlAdapterChooseZoneDefenseBudget({
			"high",
			30,
			0,
			0,
			false,
			true,
			false,
			false,
			false
		});
		const AIControlAdapterZoneDefenseBudgetResult mediumBudget = AIControlAdapterChooseZoneDefenseBudget({
			"medium",
			30 - highBudget.maxNewAssignments,
			1,
			0,
			false,
			true,
			false,
			false,
			false
		});

		expect(highBudget.maxNewAssignments > 0, "High threat should receive a capped allocation");
		expect(mediumBudget.maxNewAssignments > 0, "Second simultaneous threat should still receive a separate allocation");
		expect(highBudget.maxNewAssignments < 30, "High threat should not consume the full army blob");
		expect(mediumBudget.maxNewAssignments < 30, "Medium threat should not consume the full army blob");

		std::cout << "PASS: testTwoThreatsReceiveCappedAllocations\n";
	}

	void testActiveAllocationSuppressesRepeatedBroadMobilization()
	{
		const AIControlAdapterZoneDefenseAllocationResult result = AIControlAdapterEvaluateZoneDefenseAllocation({
			true,
			3,
			3,
			20000u,
			50000u,
			100000u,
			8,
			8,
			100.0f,
			100.0f,
			120.0f,
			110.0f,
			300.0f
		});

		expect(!result.shouldIssueCommand, "Active same-severity allocation should suppress broad remobilization");
		expectEq(std::string(result.reason), std::string("min_hold"), "Suppression reason should be min_hold");

		std::cout << "PASS: testActiveAllocationSuppressesRepeatedBroadMobilization\n";
	}

	void testMinHoldBlocksEqualSeverityRetask()
	{
		const AIControlAdapterZoneDefenseAllocationResult result = AIControlAdapterEvaluateZoneDefenseAllocation({
			true,
			2,
			2,
			25000u,
			60000u,
			100000u,
			5,
			8,
			100.0f,
			100.0f,
			130.0f,
			120.0f,
			300.0f
		});

		expect(!result.shouldIssueCommand, "Equal-severity threat should not steal an allocation during minimum hold");
		expectEq(std::string(result.reason), std::string("min_hold"), "Expected min_hold reason");

		std::cout << "PASS: testMinHoldBlocksEqualSeverityRetask\n";
	}

	void testCriticalThreatOverride()
	{
		const AIControlAdapterZoneDefenseBudgetResult budget = AIControlAdapterChooseZoneDefenseBudget({
			"critical",
			18,
			2,
			0,
			false,
			false,
			true,
			true,
			true
		});

		expect(budget.criticalOverride, "Critical threat should enable explicit override");
		expect(budget.allowFrontDonors, "Critical threat may pull from front donors");
		expect(budget.maxNewAssignments > 0, "Critical threat should still receive help under allocation pressure");
		expectEq(std::string(budget.reason), std::string("critical_preserve_other_fronts"), "Critical reason should preserve other fronts");

		std::cout << "PASS: testCriticalThreatOverride\n";
	}

	void testLocalReservesPreserved()
	{
		const AIControlAdapterZoneDefenseBudgetResult mainBudget = AIControlAdapterChooseZoneDefenseBudget({
			"high",
			10,
			0,
			0,
			true,
			false,
			false,
			false,
			false
		});
		const AIControlAdapterZoneDefenseBudgetResult frontierLowBudget = AIControlAdapterChooseZoneDefenseBudget({
			"low",
			20,
			0,
			0,
			false,
			false,
			true,
			true,
			false
		});

		expectEq(mainBudget.localReserve, 8, "Main base reserve should be preserved");
		expect(mainBudget.maxNewAssignments <= 2, "Main base budget should not spend below reserve");
		expectEq(frontierLowBudget.maxNewAssignments, 0, "Low threat should not pull from active/front zones");
		expectEq(std::string(frontierLowBudget.reason), std::string("front_reserve"), "Frontier block should explain reserve protection");

		std::cout << "PASS: testLocalReservesPreserved\n";
	}

	void testReinforcementAddsOnlyDelta()
	{
		const AIControlAdapterZoneDefenseAllocationResult result = AIControlAdapterEvaluateZoneDefenseAllocation({
			true,
			2,
			3,
			70000u,
			60000u,
			120000u,
			5,
			12,
			100.0f,
			100.0f,
			110.0f,
			110.0f,
			300.0f
		});

		expect(result.shouldIssueCommand, "Escalated threat should reinforce existing allocation");
		expect(result.shouldReinforce, "Escalated threat should be marked as reinforcement");
		expectEq(result.requestedNewAssignments, 7, "Reinforcement should request only the missing delta");
		expectEq(std::string(result.reason), std::string("threat_escalated"), "Expected threat_escalated reason");

		std::cout << "PASS: testReinforcementAddsOnlyDelta\n";
	}

	void testMissingDefendersTriggerReinforcement()
	{
		const AIControlAdapterZoneDefenseAllocationResult result = AIControlAdapterEvaluateZoneDefenseAllocation({
			true,
			3,
			3,
			80000u,
			60000u,
			120000u,
			2,
			10,
			100.0f,
			100.0f,
			100.0f,
			100.0f,
			300.0f
		});

		expect(result.shouldIssueCommand, "Missing defenders should trigger reinforcement");
		expect(result.shouldReinforce, "Missing defenders should be marked as reinforcement");
		expectEq(result.requestedNewAssignments, 8, "Missing defender reinforcement should request delta only");
		expectEq(std::string(result.reason), std::string("defenders_missing"), "Expected defenders_missing reason");

		std::cout << "PASS: testMissingDefendersTriggerReinforcement\n";
	}

	void runAllPhase78Tests()
	{
		std::cout << "\nRunning Phase 7.8 Multi-Front Defense Allocation tests...\n";

		testTwoThreatsReceiveCappedAllocations();
		testActiveAllocationSuppressesRepeatedBroadMobilization();
		testMinHoldBlocksEqualSeverityRetask();
		testCriticalThreatOverride();
		testLocalReservesPreserved();
		testReinforcementAddsOnlyDelta();
		testMissingDefendersTriggerReinforcement();

		std::cout << "All Phase 7.8 Multi-Front Defense Allocation tests passed!\n";
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

namespace Phase79
{
	void testWmdStrikeWithoutLocalEnemySuppressesDefense()
	{
		const AIControlAdapterZoneThreatSourceResult result = AIControlAdapterClassifyZoneThreatSource({
			0,
			0,
			true,
			0.45f,
			900.0f,
			2,
			1
		});

		expect(std::string(result.type) == "wmd_strike", "Severe recent-WMD damage with no local enemies should classify as wmd_strike");
		expect(std::string(result.response) == "hold_rebuild_recover", "WMD-only strike should choose hold/rebuild/recover response");
		expect(std::string(result.reason) == "recent_wmd_no_local_enemy", "WMD-only strike should expose concrete reason");
		std::cout << "PASS: Phase79::testWmdStrikeWithoutLocalEnemySuppressesDefense\n";
	}

	void testWmdDamageWithLocalEnemyAllowsDefense()
	{
		const AIControlAdapterZoneThreatSourceResult result = AIControlAdapterClassifyZoneThreatSource({
			3,
			0,
			true,
			0.45f,
			900.0f,
			2,
			0
		});

		expect(std::string(result.type) == "unit_attack", "Visible local enemies should override WMD-only classification");
		expect(std::string(result.response) == "defend", "Visible local enemies should allow defense response");
		expect(std::string(result.reason) == "local_enemy_units_recent_wmd", "Recent WMD plus real units should explain local units are present");
		std::cout << "PASS: Phase79::testWmdDamageWithLocalEnemyAllowsDefense\n";
	}

	void testUnitAttackStillMobilizesDefense()
	{
		const AIControlAdapterZoneThreatSourceResult result = AIControlAdapterClassifyZoneThreatSource({
			2,
			0,
			false,
			0.08f,
			120.0f,
			1,
			0
		});

		expect(std::string(result.type) == "unit_attack", "Local visible enemy damage should classify as unit_attack");
		expect(std::string(result.response) == "defend", "Unit attack should preserve normal defense behavior");
		std::cout << "PASS: Phase79::testUnitAttackStillMobilizesDefense\n";
	}

	void testUnknownDamageUsesHoldRecover()
	{
		const AIControlAdapterZoneThreatSourceResult result = AIControlAdapterClassifyZoneThreatSource({
			0,
			0,
			false,
			0.08f,
			120.0f,
			1,
			0
		});

		expect(std::string(result.type) == "unknown_damage", "Damage without evidence should classify as unknown_damage");
		expect(std::string(result.response) == "hold_rebuild_recover", "Unknown damage should not request full mobile defense");
		expect(std::string(result.reason) == "damage_no_local_enemy", "Unknown damage should expose no-local-enemy reason");
		std::cout << "PASS: Phase79::testUnknownDamageUsesHoldRecover\n";
	}

	void testArtilleryThreatUsesCounterbattery()
	{
		const AIControlAdapterZoneThreatSourceResult result = AIControlAdapterClassifyZoneThreatSource({
			0,
			1,
			false,
			0.12f,
			180.0f,
			1,
			0
		});

		expect(std::string(result.type) == "artillery_attack", "Visible artillery without local enemies should classify as artillery_attack");
		expect(std::string(result.response) == "counterbattery", "Artillery attack should prefer counterbattery response");
		expect(std::string(result.reason) == "enemy_artillery_detected", "Artillery attack should expose artillery evidence reason");
		std::cout << "PASS: Phase79::testArtilleryThreatUsesCounterbattery\n";
	}

	void testNonUnitThreatAllocationDoesNotIssueDefense()
	{
		const AIControlAdapterZoneDefenseAllocationResult result = AIControlAdapterEvaluateZoneDefenseAllocation({
			false,
			0,
			2,
			10000u,
			0u,
			0u,
			0,
			0,
			0.0f,
			0.0f,
			100.0f,
			100.0f,
			300.0f
		});

		expect(!result.shouldIssueCommand, "No requested defenders should prevent mobile defense allocation");
		expect(std::string(result.reason) == "no_budget", "No requested defenders should report no budget");
		std::cout << "PASS: Phase79::testNonUnitThreatAllocationDoesNotIssueDefense\n";
	}

	void runAllPhase79Tests()
	{
		std::cout << "\nRunning Phase 7.9 Increment 2 threat-source tests...\n";
		testWmdStrikeWithoutLocalEnemySuppressesDefense();
		testWmdDamageWithLocalEnemyAllowsDefense();
		testUnitAttackStillMobilizesDefense();
		testUnknownDamageUsesHoldRecover();
		testArtilleryThreatUsesCounterbattery();
		testNonUnitThreatAllocationDoesNotIssueDefense();
		std::cout << "All Phase 7.9 Increment 2 tests passed!\n";
	}
}

namespace Phase793
{
	void testFrontierRequestsMoreStaticDefenseThanRear()
	{
		const AIControlAdapterStaticDefensePolicyResult rear = AIControlAdapterChooseStaticDefensePolicy({
			"rear", false, false, false, false, false, false,
			0, 0, 0, 0, 0, 0
		});
		const AIControlAdapterStaticDefensePolicyResult frontier = AIControlAdapterChooseStaticDefensePolicy({
			"frontier", false, true, true, false, false, false,
			0, 0, 0, 0, 0, 0
		});

		expect(frontier.desiredTunnels > rear.desiredTunnels, "Frontier zones should request more tunnels than rear zones");
		expect(frontier.desiredStingers > rear.desiredStingers, "Frontier zones should request more stingers than rear zones");
		expect(frontier.shouldBuildTunnel, "Undersupplied frontier should request a tunnel");
		expect(frontier.shouldBuildStinger, "Undersupplied frontier should request a stinger");
		std::cout << "PASS: Phase793::testFrontierRequestsMoreStaticDefenseThanRear\n";
	}

	void testRepeatedAttackRaisesStingerDesired()
	{
		const AIControlAdapterStaticDefensePolicyResult normal = AIControlAdapterChooseStaticDefensePolicy({
			"developed_rear", false, true, false, false, false, false,
			1, 0, 0, 0, 0, 0
		});
		const AIControlAdapterStaticDefensePolicyResult repeated = AIControlAdapterChooseStaticDefensePolicy({
			"developed_rear", false, true, false, false, false, true,
			1, 0, 0, 0, 0, 0
		});

		expect(repeated.desiredStingers == normal.desiredStingers + 1, "Repeated attacks should add one desired stinger");
		expect(std::string(repeated.reason) == "repeated_attack", "Repeated attack should expose explicit reason");
		std::cout << "PASS: Phase793::testRepeatedAttackRaisesStingerDesired\n";
	}

	void testReservedStaticDefensePreventsDuplicateSpam()
	{
		const AIControlAdapterStaticDefensePolicyResult result = AIControlAdapterChooseStaticDefensePolicy({
			"frontier", false, true, true, false, false, false,
			1, 1, 0, 0, 1, 1
		});

		expect(result.effectiveTunnels == 2, "Reserved tunnel build task should count toward desired tunnel total");
		expect(result.effectiveStingers == 2, "Reserved stinger build task should count toward desired stinger total");
		expect(!result.shouldBuildTunnel, "Policy should not duplicate tunnel while effective total meets desired");
		expect(!result.shouldBuildStinger, "Policy should not duplicate stinger while effective total meets desired");
		std::cout << "PASS: Phase793::testReservedStaticDefensePreventsDuplicateSpam\n";
	}

	void testPalaceRedundancyRequiresReserveAndCashFloat()
	{
		const AIControlAdapterPalaceRedundancyResult allowed = AIControlAdapterEvaluatePalaceRedundancy({
			"frontier", true, true, true, false,
			true, false, 20000u,
			1, 0, 0, 0
		});
		const AIControlAdapterPalaceRedundancyResult blockedReserve = AIControlAdapterEvaluatePalaceRedundancy({
			"frontier", true, true, true, false,
			false, false, 20000u,
			1, 0, 0, 0
		});
		const AIControlAdapterPalaceRedundancyResult blockedUrgent = AIControlAdapterEvaluatePalaceRedundancy({
			"frontier", true, true, true, false,
			true, true, 20000u,
			1, 0, 0, 0
		});

		expect(allowed.shouldBuild, "Mature frontier zone should request redundant Palace when reserve and cash are healthy");
		expect(allowed.spendAllowed, "Allowed redundant Palace should mark spend allowed");
		expect(!blockedReserve.shouldBuild, "Palace redundancy should not bypass reserve policy");
		expect(std::string(blockedReserve.reason) == "reserve_protected", "Reserve block should expose concrete reason");
		expect(!blockedUrgent.shouldBuild, "Palace redundancy should not bypass urgent expansion");
		expect(std::string(blockedUrgent.reason) == "urgent_expansion_priority", "Urgent expansion block should expose concrete reason");
		std::cout << "PASS: Phase793::testPalaceRedundancyRequiresReserveAndCashFloat\n";
	}

	void runAllPhase793Tests()
	{
		std::cout << "\nRunning Phase 7.9 Increment 3 static-defense and Palace tests...\n";
		testFrontierRequestsMoreStaticDefenseThanRear();
		testRepeatedAttackRaisesStingerDesired();
		testReservedStaticDefensePreventsDuplicateSpam();
		testPalaceRedundancyRequiresReserveAndCashFloat();
		std::cout << "All Phase 7.9 Increment 3 tests passed!\n";
	}
}

namespace Phase794
{
	void testNukeCannonCreatesCounterbatteryDemand()
	{
		expect(AIControlAdapterIsBattlefieldArtilleryTemplate("ChinaNukeCannon", false), "Visible Nuke Cannon should classify as battlefield artillery");
		expect(AIControlAdapterIsBattlefieldArtilleryTemplate("ChinaVehicleInfernoCannon", false), "Visible Inferno Cannon should classify as battlefield artillery");
		const AIControlAdapterCounterbatteryPolicyResult result = AIControlAdapterChooseCounterbatteryPolicy({
			1,
			3,
			0,
			true,
			0,
			0,
			2
		});
		expect(result.shouldAssign, "Available counter units should be assigned to visible artillery");
		expectEq(result.desiredGroups, 1, "Visible artillery should request one bounded counterbattery group");
		expectEq(std::string(result.reason), std::string("mobile_siege_visible"), "Counterbattery demand should expose mobile_siege_visible reason");
		std::cout << "PASS: Phase794::testNukeCannonCreatesCounterbatteryDemand\n";
	}

	void testProjectileTemplatesDoNotCreateMobileSiegeDemand()
	{
		const AIControlAdapterMobileSiegeTemplateResult shell = AIControlAdapterClassifyMobileSiegeTemplate(
			"StrategyCenterArtilleryShell",
			false,
			true);
		const AIControlAdapterMobileSiegeTemplateResult artilleryCannon = AIControlAdapterClassifyMobileSiegeTemplate(
			"ChinaArtilleryCannon",
			false,
			true);
		const AIControlAdapterMobileSiegeTemplateResult nonEnemy = AIControlAdapterClassifyMobileSiegeTemplate(
			"ChinaVehicleNukeCannon",
			false,
			false);
		expect(!shell.accepted, "Artillery shell projectile should not classify as mobile siege");
		expectEq(std::string(shell.reason), std::string("projectile_rejected"), "Projectile rejection reason should be explicit");
		expect(!artilleryCannon.accepted, "Artillery barrage cannon object should not classify as mobile siege");
		expectEq(std::string(artilleryCannon.reason), std::string("projectile_rejected"), "Artillery cannon rejection reason should be explicit");
		expect(!nonEnemy.accepted, "Friendly or neutral artillery should not classify as enemy mobile siege");
		expectEq(std::string(nonEnemy.reason), std::string("not_enemy"), "Non-enemy rejection reason should be explicit");
		std::cout << "PASS: Phase794::testProjectileTemplatesDoNotCreateMobileSiegeDemand\n";
	}

	void testWmdStructureDoesNotCreateMobileCounterbatteryDemand()
	{
		expect(!AIControlAdapterIsBattlefieldArtilleryTemplate("ChinaNuclearMissileLauncher", true), "Enemy WMD structure should not classify as battlefield artillery");
		expect(!AIControlAdapterIsBattlefieldArtilleryTemplate("GLAScudStorm", true), "SCUD Storm structure should remain WMD defense, not mobile artillery counterbattery");
		const AIControlAdapterCounterbatteryPolicyResult result = AIControlAdapterChooseCounterbatteryPolicy({
			0,
			3,
			0,
			true,
			0,
			0,
			2
		});
		expect(!result.shouldAssign, "No battlefield artillery should not assign counterbattery");
		expectEq(std::string(result.reason), std::string("no_artillery_threat"), "No-threat reason should be explicit");
		std::cout << "PASS: Phase794::testWmdStructureDoesNotCreateMobileCounterbatteryDemand\n";
	}

	void testNoCounterAvailableRequestsBoundedProduction()
	{
		const AIControlAdapterCounterbatteryPolicyResult result = AIControlAdapterChooseCounterbatteryPolicy({
			1,
			0,
			0,
			true,
			0,
			0,
			2
		});
		expect(!result.shouldAssign, "No available counters should not assign a task");
		expect(result.productionNeeded, "Missing counters with prerequisites should request bounded production");
		expectEq(std::string(result.reason), std::string("production_needed"), "Production-needed reason should be explicit");
		std::cout << "PASS: Phase794::testNoCounterAvailableRequestsBoundedProduction\n";
	}

	void testMobileScudProductionIsBounded()
	{
		const AIControlAdapterCounterbatteryPolicyResult capped = AIControlAdapterChooseCounterbatteryPolicy({
			1,
			0,
			0,
			true,
			2,
			0,
			2
		});
		expect(!capped.productionNeeded, "Counterbattery SCUD production should stop at bounded cap");
		expectEq(std::string(capped.reason), std::string("no_counter_available"), "Cap block should not restore generic WMD baseline reserve");
		std::cout << "PASS: Phase794::testMobileScudProductionIsBounded\n";
	}

	void testActiveTaskSuppressesDuplicateCounterbattery()
	{
		const AIControlAdapterCounterbatteryPolicyResult result = AIControlAdapterChooseCounterbatteryPolicy({
			1,
			3,
			1,
			true,
			0,
			0,
			2
		});
		expect(!result.shouldAssign, "Existing active task should suppress duplicate broad response");
		expectEq(std::string(result.reason), std::string("active_task_exists"), "Duplicate suppression reason should be explicit");
		std::cout << "PASS: Phase794::testActiveTaskSuppressesDuplicateCounterbattery\n";
	}

	void testLateGameVehicleMixRequestsRocketBuggies()
	{
		const AIControlAdapterRocketBuggyMixResult result = AIControlAdapterChooseRocketBuggyMix({
			true,
			true,
			false,
			0,
			0,
			6,
			4,
			0
		});
		expect(result.productionNeeded, "Late-game vehicle mix should request Rocket Buggies once prerequisites are ready");
		expect(result.desiredBuggies >= 2, "Rocket Buggy target should be a bounded share of late-game vehicles");
		expectEq(std::string(result.reason), std::string("late_game_mix"), "Late-game mix reason should be explicit");
		std::cout << "PASS: Phase794::testLateGameVehicleMixRequestsRocketBuggies\n";
	}

	void testMobileSiegeThreatRaisesRocketBuggyDesired()
	{
		const AIControlAdapterRocketBuggyMixResult baseline = AIControlAdapterChooseRocketBuggyMix({
			true,
			true,
			false,
			0,
			0,
			4,
			4,
			0
		});
		const AIControlAdapterRocketBuggyMixResult siege = AIControlAdapterChooseRocketBuggyMix({
			true,
			true,
			true,
			0,
			0,
			4,
			4,
			0
		});
		expect(siege.desiredBuggies > baseline.desiredBuggies, "Mobile siege threat should temporarily raise desired Rocket Buggy count");
		expectEq(std::string(siege.reason), std::string("mobile_siege_counter"), "Siege mix reason should be explicit");
		std::cout << "PASS: Phase794::testMobileSiegeThreatRaisesRocketBuggyDesired\n";
	}

	void testRocketBuggyPrereqsAndQueuedCount()
	{
		const AIControlAdapterRocketBuggyMixResult missingPrereq = AIControlAdapterChooseRocketBuggyMix({
			false,
			true,
			true,
			0,
			0,
			8,
			8,
			0
		});
		const AIControlAdapterRocketBuggyMixResult queuedMeetsTarget = AIControlAdapterChooseRocketBuggyMix({
			true,
			true,
			true,
			0,
			5,
			8,
			8,
			0
		});
		expect(!missingPrereq.productionNeeded, "Missing Rocket Buggy prerequisites should block production");
		expectEq(std::string(missingPrereq.reason), std::string("prereq_missing"), "Missing prereq reason should be explicit");
		expect(!queuedMeetsTarget.productionNeeded, "Queued Rocket Buggies should count toward desired target");
		std::cout << "PASS: Phase794::testRocketBuggyPrereqsAndQueuedCount\n";
	}

	void runAllPhase794Tests()
	{
		std::cout << "\nRunning Phase 7.9 Increment 4 battlefield counterbattery tests...\n";
		testNukeCannonCreatesCounterbatteryDemand();
		testProjectileTemplatesDoNotCreateMobileSiegeDemand();
		testWmdStructureDoesNotCreateMobileCounterbatteryDemand();
		testNoCounterAvailableRequestsBoundedProduction();
		testMobileScudProductionIsBounded();
		testActiveTaskSuppressesDuplicateCounterbattery();
		testLateGameVehicleMixRequestsRocketBuggies();
		testMobileSiegeThreatRaisesRocketBuggyDesired();
		testRocketBuggyPrereqsAndQueuedCount();
		std::cout << "All Phase 7.9 Increment 4 tests passed!\n";
	}
}

namespace Phase795
{
	void testHighExpansionGapBeatsHardeningLuxury()
	{
		const AIControlAdapterBrutalPressureResult result = AIControlAdapterChooseBrutalPressurePriority({
			6,
			false,
			4,
			2,
			0,
			0,
			0,
			true,
			12000u,
			true,
			false
		});
		expectEq(std::string(result.chosenPriority), std::string("urgent_expansion"), "High zone gap should outrank non-emergency hardening");
		expectEq(std::string(result.reason), std::string("distributed_survival_expansion"), "Expansion priority reason should be explicit");
		std::cout << "PASS: Phase795::testHighExpansionGapBeatsHardeningLuxury\n";
	}

	void testStaleFoundationRecoveryBeatsLuxurySpend()
	{
		const AIControlAdapterBrutalPressureResult result = AIControlAdapterChooseBrutalPressurePriority({
			0,
			false,
			3,
			2,
			0,
			1,
			0,
			true,
			30000u,
			true,
			false
		});
		expectEq(std::string(result.chosenPriority), std::string("foundation_recovery"), "Stale strategic foundations should outrank luxury hardening");
		expectEq(std::string(result.reason), std::string("stale_foundation_recovery"), "Foundation recovery reason should be explicit");
		std::cout << "PASS: Phase795::testStaleFoundationRecoveryBeatsLuxurySpend\n";
	}

	void testLocalWorkerLiquidityRespectsGlobalCap()
	{
		const AIControlAdapterLocalWorkerLiquidityResult capped = AIControlAdapterChooseLocalWorkerLiquidity({
			80,
			80,
			20000u,
			0,
			2,
			true
		});
		const AIControlAdapterLocalWorkerLiquidityResult allowed = AIControlAdapterChooseLocalWorkerLiquidity({
			20,
			80,
			20000u,
			0,
			2,
			true
		});
		expect(!capped.shouldQueue, "Local worker liquidity should stop at global worker cap");
		expectEq(std::string(capped.reason), std::string("worker_cap_reached"), "Worker cap reason should be explicit");
		expect(allowed.shouldQueue, "Local worker liquidity should queue when below cap and local gap exists");
		expectEq(std::string(allowed.reason), std::string("queued_local_worker"), "Allowed local worker reason should be explicit");
		std::cout << "PASS: Phase795::testLocalWorkerLiquidityRespectsGlobalCap\n";
	}

	void testLocalWorkerLiquidityProtectsReserve()
	{
		const AIControlAdapterLocalWorkerLiquidityResult result = AIControlAdapterChooseLocalWorkerLiquidity({
			20,
			80,
			1200u,
			0,
			2,
			true
		});
		expect(!result.shouldQueue, "Local worker liquidity should not spend below cash float threshold");
		expectEq(std::string(result.reason), std::string("cash_reserved"), "Cash-reserve worker block should be explicit");
		std::cout << "PASS: Phase795::testLocalWorkerLiquidityProtectsReserve\n";
	}

	void testNormalAttackOnlyAfterSurvivalBudgetsSatisfied()
	{
		const AIControlAdapterBrutalPressureResult blockedByDefense = AIControlAdapterChooseBrutalPressurePriority({
			0,
			false,
			2,
			0,
			0,
			0,
			0,
			true,
			20000u,
			true,
			false
		});
		const AIControlAdapterBrutalPressureResult attackReady = AIControlAdapterChooseBrutalPressurePriority({
			0,
			false,
			0,
			0,
			0,
			0,
			0,
			true,
			20000u,
			true,
			false
		});
		expectEq(std::string(blockedByDefense.chosenPriority), std::string("harden_frontier"), "Defense gap should defer normal attacks");
		expectEq(std::string(attackReady.chosenPriority), std::string("offensive_pressure"), "Normal attacks should resume when survival budgets are satisfied");
		expectEq(std::string(attackReady.reason), std::string("defense_budget_satisfied"), "Attack-ready reason should be explicit");
		std::cout << "PASS: Phase795::testNormalAttackOnlyAfterSurvivalBudgetsSatisfied\n";
	}

	void testEmergencyUnitAttackOverridesLowerPriorityWork()
	{
		const AIControlAdapterBrutalPressureResult result = AIControlAdapterChooseBrutalPressurePriority({
			8,
			true,
			5,
			3,
			2,
			1,
			1,
			true,
			50000u,
			true,
			true
		});
		expectEq(std::string(result.chosenPriority), std::string("emergency_survival"), "Active unit attacks should override lower-priority work");
		expectEq(std::string(result.reason), std::string("active_unit_attack"), "Emergency reason should be explicit");
		std::cout << "PASS: Phase795::testEmergencyUnitAttackOverridesLowerPriorityWork\n";
	}

	void runAllPhase795Tests()
	{
		std::cout << "\nRunning Phase 7.9 Increment 5 brutal-pressure integration tests...\n";
		testHighExpansionGapBeatsHardeningLuxury();
		testStaleFoundationRecoveryBeatsLuxurySpend();
		testLocalWorkerLiquidityRespectsGlobalCap();
		testLocalWorkerLiquidityProtectsReserve();
		testNormalAttackOnlyAfterSurvivalBudgetsSatisfied();
		testEmergencyUnitAttackOverridesLowerPriorityWork();
		std::cout << "All Phase 7.9 Increment 5 tests passed!\n";
	}
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

	// Phase 7.8: Multi-front defense allocation tests
	Phase78::runAllPhase78Tests();

	// Phase 7.9 Increment 2: Threat-source classification tests
	Phase79::runAllPhase79Tests();

	// Phase 7.9 Increment 3: Static-defense density and Palace redundancy tests
	Phase793::runAllPhase793Tests();

	// Phase 7.9 Increment 4: Battlefield artillery counterbattery tests
	Phase794::runAllPhase794Tests();

	// Phase 7.9 Increment 5: Brutal-pressure integration policy tests
	Phase795::runAllPhase795Tests();

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
