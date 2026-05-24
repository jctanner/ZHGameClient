/**
 * AIControlAdapterTechManagerTests.cpp
 *
 * Unit tests for AIControlAdapterTechManager.
 *
 * Tests verify Phase 1 implementation requirements:
 * - Science planning continues after candidate-specific failures
 * - Upgrade planning continues after producer-specific failures
 * - Failure memory tracks invalid combinations
 * - Retry logic respects delay timers
 * - Candidate names are available for logging
 */

#include "GameClient/AIControlAdapter/AIControlAdapterTechManager.h"

#include <cstdlib>
#include <iostream>
#include <map>
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

	void expectEq(const std::string& actual, const std::string& expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << message << " actual=\"" << actual << "\" expected=\"" << expected << "\"\n";
			std::exit(1);
		}
	}

	// Test helper: simple science plan
	const char* const kTestSciencePlan[] = {
		"SCIENCE_First",
		"SCIENCE_Second",
		"SCIENCE_Third"
	};

	// Test helper: simple upgrade plan
	struct TestUpgradePlanEntry
	{
		const char* producerKind;
		const char* upgradeName;
	};

	const TestUpgradePlanEntry kTestUpgradePlan[] = {
		{ "palace", "Upgrade_First" },
		{ "black_market", "Upgrade_Second" },
		{ "palace", "Upgrade_Third" }
	};

	// Test helper: track command attempts
	struct CommandAttempt
	{
		std::string command;
		std::string candidateName;
		std::string producerKind;
		std::string reason;
		bool success;
	};

	std::vector<CommandAttempt> g_commandAttempts;

	// Mock tryCommand functions that record attempts
	bool mockTryScienceCommand(const char* category, const char* command, const char* scienceName, std::string& reason)
	{
		CommandAttempt attempt;
		attempt.command = command;
		attempt.candidateName = scienceName;
		attempt.reason = reason;
		attempt.success = false;

		// Check if this command should succeed based on pre-set reason
		if (reason.empty())
		{
			attempt.success = true;
			attempt.reason = "ok";
		}
		else
		{
			attempt.success = false;
		}

		g_commandAttempts.push_back(attempt);
		return attempt.success;
	}

	bool mockTryUpgradeCommand(const char* category, const char* command, const char* producerKind, const char* upgradeName, std::string& reason)
	{
		CommandAttempt attempt;
		attempt.command = command;
		attempt.candidateName = upgradeName;
		attempt.producerKind = producerKind;
		attempt.reason = reason;
		attempt.success = false;

		// Check if this command should succeed based on pre-set reason
		if (reason.empty())
		{
			attempt.success = true;
			attempt.reason = "ok";
		}
		else
		{
			attempt.success = false;
		}

		g_commandAttempts.push_back(attempt);
		return attempt.success;
	}

	bool mockPlayerHasUpgrade(const char* upgradeName)
	{
		// For testing, assume no upgrades owned initially
		return false;
	}

	// Helper to create tryCommand wrapper with predetermined failure
	auto makeScienceCommandWithFailure(const std::string& failOnCandidate, const std::string& failReason)
	{
		return [failOnCandidate, failReason](const char* category, const char* command, const char* scienceName, std::string& reason) -> bool
		{
			if (std::string(scienceName) == failOnCandidate)
			{
				reason = failReason;
				return mockTryScienceCommand(category, command, scienceName, reason);
			}
			reason = "";  // Success for others
			return mockTryScienceCommand(category, command, scienceName, reason);
		};
	}

	auto makeUpgradeCommandWithFailure(const std::string& failOnCandidate, const std::string& failReason)
	{
		return [failOnCandidate, failReason](const char* category, const char* command, const char* producerKind, const char* upgradeName, std::string& reason) -> bool
		{
			if (std::string(upgradeName) == failOnCandidate)
			{
				reason = failReason;
				return mockTryUpgradeCommand(category, command, producerKind, upgradeName, reason);
			}
			reason = "";  // Success for others
			return mockTryUpgradeCommand(category, command, producerKind, upgradeName, reason);
		};
	}
}

// Test: Science planning continues after candidate-specific failure
void testScienceContinuesAfterCandidateFailure()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = makeScienceCommandWithFailure("SCIENCE_First", "science_not_purchasable");

	ScienceCandidateResult result = manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,  // hasScienceEconomy
		true,  // hasScienceAdvanced
		5000u, // money
		tryCmd,
		1000u  // currentTick
	);

	expectEq((int)g_commandAttempts.size(), 2, "ScienceContinuesAfterCandidateFailure: attempt count");
	expectEq(g_commandAttempts[0].candidateName, std::string("SCIENCE_First"), "ScienceContinuesAfterCandidateFailure: first candidate");
	expect(!g_commandAttempts[0].success, "ScienceContinuesAfterCandidateFailure: first should fail");
	expectEq(g_commandAttempts[1].candidateName, std::string("SCIENCE_Second"), "ScienceContinuesAfterCandidateFailure: second candidate");
	expect(g_commandAttempts[1].success, "ScienceContinuesAfterCandidateFailure: second should succeed");
	expect(result.attempted, "ScienceContinuesAfterCandidateFailure: result attempted");
	expectEq(result.candidateName, std::string("SCIENCE_Second"), "ScienceContinuesAfterCandidateFailure: result candidate");

	std::cout << "PASS: testScienceContinuesAfterCandidateFailure\n";
}

// Test: Upgrade planning continues after producer-specific failure
void testUpgradeContinuesAfterProducerFailure()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = makeUpgradeCommandWithFailure("Upgrade_First", "producer_cannot_make_upgrade");

	std::map<std::string, int> producerCounts;
	producerCounts["palace"] = 1;
	producerCounts["black_market"] = 1;

	UpgradeCandidateResult result = manager.ChooseAndAttemptUpgrade(
		kTestUpgradePlan,
		3,
		mockPlayerHasUpgrade,
		producerCounts,
		5000u, // money
		tryCmd,
		1000u  // currentTick
	);

	expectEq((int)g_commandAttempts.size(), 2, "UpgradeContinuesAfterProducerFailure: attempt count");
	expectEq(g_commandAttempts[0].candidateName, std::string("Upgrade_First"), "UpgradeContinuesAfterProducerFailure: first candidate");
	expect(!g_commandAttempts[0].success, "UpgradeContinuesAfterProducerFailure: first should fail");
	expectEq(g_commandAttempts[1].candidateName, std::string("Upgrade_Second"), "UpgradeContinuesAfterProducerFailure: second candidate");
	expect(g_commandAttempts[1].success, "UpgradeContinuesAfterProducerFailure: second should succeed");
	expect(result.attempted, "UpgradeContinuesAfterProducerFailure: result attempted");
	expectEq(result.candidateName, std::string("Upgrade_Second"), "UpgradeContinuesAfterProducerFailure: result candidate");

	std::cout << "PASS: testUpgradeContinuesAfterProducerFailure\n";
}

// Test: Transient failure stops scanning for this tick
void testTransientFailureStopsScanning()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = makeScienceCommandWithFailure("SCIENCE_First", "no_money");

	ScienceCandidateResult result = manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd,
		1000u
	);

	expectEq((int)g_commandAttempts.size(), 1, "TransientFailureStopsScanning: attempt count");
	expectEq(g_commandAttempts[0].candidateName, std::string("SCIENCE_First"), "TransientFailureStopsScanning: first candidate");
	expect(!g_commandAttempts[0].success, "TransientFailureStopsScanning: should fail");
	expect(!result.attempted, "TransientFailureStopsScanning: result not attempted");
	expectEq(result.candidateName, std::string("SCIENCE_First"), "TransientFailureStopsScanning: result candidate");
	expectEq(result.resultReason, std::string("no_money"), "TransientFailureStopsScanning: result reason");

	std::cout << "PASS: testTransientFailureStopsScanning\n";
}

// Test: Failure memory prevents retry before delay
void testFailureMemoryPreventsRetry()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = makeScienceCommandWithFailure("SCIENCE_First", "science_not_purchasable");

	manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd,
		1000u  // tick 1000
	);

	expectEq((int)g_commandAttempts.size(), 2, "FailureMemoryPreventsRetry: first run attempt count");
	g_commandAttempts.clear();

	// Second attempt at tick 2000 (within retry delay): should skip SCIENCE_First
	manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd,
		2000u  // tick 2000, within 10s delay
	);

	expectEq((int)g_commandAttempts.size(), 1, "FailureMemoryPreventsRetry: second run attempt count");
	expectEq(g_commandAttempts[0].candidateName, std::string("SCIENCE_Second"), "FailureMemoryPreventsRetry: skipped first");

	std::cout << "PASS: testFailureMemoryPreventsRetry\n";
}

// Test: Failure memory allows retry after delay
void testFailureMemoryAllowsRetryAfterDelay()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd1 = makeScienceCommandWithFailure("SCIENCE_First", "science_not_purchasable");

	manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd1,
		1000u  // tick 1000
	);

	g_commandAttempts.clear();

	// Second attempt after delay: SCIENCE_First should succeed now
	auto tryCmd2 = makeScienceCommandWithFailure("SCIENCE_Second", "no_money");

	ScienceCandidateResult result = manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd2,
		15000u  // tick 15000, after 10s delay
	);

	expectEq((int)g_commandAttempts.size(), 1, "FailureMemoryAllowsRetryAfterDelay: attempt count");
	expectEq(g_commandAttempts[0].candidateName, std::string("SCIENCE_First"), "FailureMemoryAllowsRetryAfterDelay: first candidate retried");
	expect(g_commandAttempts[0].success, "FailureMemoryAllowsRetryAfterDelay: should succeed");
	expect(result.attempted, "FailureMemoryAllowsRetryAfterDelay: result attempted");

	std::cout << "PASS: testFailureMemoryAllowsRetryAfterDelay\n";
}

// Test: Reset clears failure memory
void testResetClearsFailureMemory()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = makeScienceCommandWithFailure("SCIENCE_First", "science_not_purchasable");

	manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd,
		1000u
	);

	expect(!manager.GetFailureMemory().empty(), "ResetClearsFailureMemory: memory should have entries");

	manager.Reset();
	expect(manager.GetFailureMemory().empty(), "ResetClearsFailureMemory: memory should be clear after reset");

	std::cout << "PASS: testResetClearsFailureMemory\n";
}

// Test: Candidate name is available in result for logging
void testCandidateNameAvailableForLogging()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = makeScienceCommandWithFailure("SCIENCE_First", "science_not_purchasable");

	ScienceCandidateResult result = manager.ChooseAndAttemptScience(
		kTestSciencePlan,
		3,
		true,
		true,
		5000u,
		tryCmd,
		1000u
	);

	expect(!result.candidateName.empty(), "CandidateNameAvailableForLogging: candidate name should be populated");
	expect(result.candidateName == "SCIENCE_First" || result.candidateName == "SCIENCE_Second",
	       "CandidateNameAvailableForLogging: candidate name should be valid");

	std::cout << "PASS: testCandidateNameAvailableForLogging\n";
}

// Test: TechManager attempts all sciences in plan regardless of hasScienceEconomy/Advanced flags
// These flags were removed because they incorrectly checked building counts instead of rank.
// The game's Player::isCapableOfPurchasingScience() handles actual prerequisite checks.
void testGLARankPrerequisitesHandledByGame()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	const char* const sciencePlan[] = {
		"SCIENCE_ScudLauncher",   // Rank 1 requirement (not building requirement!)
		"SCIENCE_MarauderTank",   // Rank 1 requirement
		"SCIENCE_CashBounty1"     // Rank 3 requirement
	};

	// Mock command that simulates game prerequisite checks
	// ScudLauncher and MarauderTank succeed (rank 1+), CashBounty1 fails (needs rank 3)
	auto tryCmd = [](const char* cat, const char* cmd, const char* name, std::string& reason) -> bool
	{
		if (std::string(name) == "SCIENCE_CashBounty1")
		{
			reason = "science_not_purchasable";  // Rank 3 not met
			return mockTryScienceCommand(cat, cmd, name, reason);
		}
		reason = "";  // Success for rank 1 sciences
		return mockTryScienceCommand(cat, cmd, name, reason);
	};

	// TechManager should attempt all sciences, letting the game check prerequisites
	ScienceCandidateResult result = manager.ChooseAndAttemptScience(
		sciencePlan,
		3,
		false,  // These flags no longer control what TechManager attempts
		false,
		5000u,
		tryCmd,
		1000u
	);

	// Should have attempted SCIENCE_ScudLauncher and succeeded
	expectEq((int)g_commandAttempts.size(), 1, "GLA rank prerequisites: one science attempted");
	expectEq(g_commandAttempts[0].candidateName, std::string("SCIENCE_ScudLauncher"), "GLA rank prerequisites: first science");
	expect(g_commandAttempts[0].success, "GLA rank prerequisites: first science succeeded");
	expect(result.attempted, "GLA rank prerequisites: result attempted");

	std::cout << "PASS: testGLARankPrerequisitesHandledByGame\n";
}

// Test: Rank 2 with ScudLauncher-only strategy saves point for Rank 3 Cash Bounty
// Strategy: Buy ScudLauncher at Rank 1, intentionally save Rank 2 point for Cash Bounty at Rank 3
void testRank2SavesPointForCashBounty()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	const char* const sciencePlan[] = {
		"SCIENCE_ScudLauncher",    // Already owned
		"SCIENCE_CashBounty1",     // Needs rank 3, will fail
		"SCIENCE_CashBounty2",     // Needs rank 3 + CashBounty1, will fail
		"SCIENCE_CashBounty3"      // Needs rank 3 + CashBounty2, will fail
	};

	auto tryCmd = [](const char* cat, const char* cmd, const char* name, std::string& reason) -> bool
	{
		if (std::string(name) == "SCIENCE_ScudLauncher")
		{
			reason = "science_already_owned";
			return mockTryScienceCommand(cat, cmd, name, reason);
		}
		// All Cash Bounty sciences fail at Rank 2
		reason = "science_not_purchasable";
		return mockTryScienceCommand(cat, cmd, name, reason);
	};

	ScienceCandidateResult result = manager.ChooseAndAttemptScience(
		sciencePlan,
		4,
		false,  // Flags no longer used
		false,
		5000u,
		tryCmd,
		1000u
	);

	// Should have attempted all 4 sciences, all failed (saving point for Rank 3)
	expectEq((int)g_commandAttempts.size(), 4, "Rank2 saves: all sciences attempted");
	expectEq(g_commandAttempts[0].candidateName, std::string("SCIENCE_ScudLauncher"), "Rank2 saves: ScudLauncher already owned");
	expectEq(g_commandAttempts[1].candidateName, std::string("SCIENCE_CashBounty1"), "Rank2 saves: tried CashBounty1");
	expectEq(g_commandAttempts[2].candidateName, std::string("SCIENCE_CashBounty2"), "Rank2 saves: tried CashBounty2");
	expectEq(g_commandAttempts[3].candidateName, std::string("SCIENCE_CashBounty3"), "Rank2 saves: tried CashBounty3");
	expect(!result.attempted, "Rank2 saves: no science purchased (intentional)");
	expectEq(result.candidateName, std::string("SCIENCE_CashBounty3"), "Rank2 saves: last candidate tracked");

	std::cout << "PASS: testRank2SavesPointForCashBounty\n";
}

// Test: Producer counts are respected for upgrades
void testProducerCountsRespected()
{
	AIControlAdapterTechManager manager;
	g_commandAttempts.clear();

	auto tryCmd = [](const char* cat, const char* cmd, const char* prod, const char* upg, std::string& reason) -> bool
	{
		reason = "";
		return mockTryUpgradeCommand(cat, cmd, prod, upg, reason);
	};

	// No producers available
	std::map<std::string, int> noProducers;
	noProducers["palace"] = 0;
	noProducers["black_market"] = 0;

	UpgradeCandidateResult result1 = manager.ChooseAndAttemptUpgrade(
		kTestUpgradePlan,
		3,
		mockPlayerHasUpgrade,
		noProducers,
		5000u,
		tryCmd,
		1000u
	);

	expectEq((int)g_commandAttempts.size(), 0, "ProducerCountsRespected: nothing attempted without producers");
	expect(!result1.attempted, "ProducerCountsRespected: result1 not attempted");

	g_commandAttempts.clear();

	// With producers available
	std::map<std::string, int> hasProducers;
	hasProducers["palace"] = 1;
	hasProducers["black_market"] = 1;

	UpgradeCandidateResult result2 = manager.ChooseAndAttemptUpgrade(
		kTestUpgradePlan,
		3,
		mockPlayerHasUpgrade,
		hasProducers,
		5000u,
		tryCmd,
		2000u
	);

	expect(g_commandAttempts.size() > 0, "ProducerCountsRespected: something attempted with producers");
	expect(result2.attempted, "ProducerCountsRespected: result2 attempted");

	std::cout << "PASS: testProducerCountsRespected\n";
}

int main()
{
	std::cout << "Running TechManager tests...\n";

	testScienceContinuesAfterCandidateFailure();
	testUpgradeContinuesAfterProducerFailure();
	testTransientFailureStopsScanning();
	testFailureMemoryPreventsRetry();
	testFailureMemoryAllowsRetryAfterDelay();
	testResetClearsFailureMemory();
	testCandidateNameAvailableForLogging();
	testGLARankPrerequisitesHandledByGame();
	testRank2SavesPointForCashBounty();
	testProducerCountsRespected();

	std::cout << "\nAll TechManager tests passed!\n";
	return 0;
}
