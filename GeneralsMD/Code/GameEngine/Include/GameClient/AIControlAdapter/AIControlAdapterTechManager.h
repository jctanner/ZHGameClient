/**
 * AIControlAdapterTechManager.h
 *
 * Technology management system for AI Control Adapter autonomous behavior.
 *
 * This module extracts science and upgrade planning from the main autonomy loop to provide:
 * - Candidate-specific failure handling (continue scanning after individual failures)
 * - Durable retry/failure memory for science and upgrades
 * - Producer capability validation
 * - Logging of tech attempts with candidate names
 *
 * Design principles:
 * 1. Ownership: TechManager owns all tech candidate selection and failure memory
 * 2. Separation: Read game state from inputs, emit decisions, don't execute commands
 * 3. Continuation: Candidate-specific failures don't block remaining candidates
 * 4. Memory: Track failed combinations to avoid repeated invalid attempts
 *
 * Phase 1 scope (architecture-improvement-plan-codex-2026-05-26.md):
 * - Work within AIControlAdapter code only
 * - Extract science and upgrade planning logic
 * - Add failure memory and retry logic
 * - Preserve existing command execution paths
 * - Do NOT redesign full autonomy loop yet
 *
 * Related bugs fixed:
 * - Unspent science points (science_not_purchasable aborted entire plan)
 * - Invalid upgrade spam (producer_cannot_make_upgrade blocked other upgrades)
 * - Missing candidate logging (couldn't diagnose which tech was attempted)
 */

#pragma once

#include <string>
#include <map>
#include <set>
#include <functional>

/**
 * Failure reason categories for tech attempts.
 *
 * Categorizes failures to determine retry strategy:
 * - PERMANENT: Never retry this candidate (unavailable for this faction)
 * - PRODUCER_SPECIFIC: Don't retry with this producer, try others
 * - TRANSIENT: Retry later (temporary state like no_money)
 * - CANDIDATE_SPECIFIC: Don't retry this candidate this tick, try next
 */
enum class TechFailureCategory
{
	PERMANENT,           // Don't ever retry (e.g., tech not available for faction)
	PRODUCER_SPECIFIC,   // Try different producer (e.g., palace can't make this upgrade)
	TRANSIENT,           // Retry later (e.g., no money, queue full)
	CANDIDATE_SPECIFIC,  // Skip this candidate for now, try next (e.g., science_not_purchasable)
};

/**
 * Memory entry for a failed tech attempt.
 *
 * Tracks failures to avoid repeated invalid attempts and implement smart retry logic.
 */
struct TechFailureMemory
{
	std::string candidateName;      // Science or upgrade name that failed
	std::string producerKind;       // Producer kind (for upgrades) or empty (for science)
	std::string reason;             // Failure reason string
	TechFailureCategory category;   // Failure category for retry logic
	unsigned int firstSeenTick;     // First time this failure was observed
	unsigned int lastSeenTick;      // Most recent time this failure was observed
	unsigned int retryAfterTick;    // Don't retry until after this tick

	TechFailureMemory()
		: firstSeenTick(0)
		, lastSeenTick(0)
		, retryAfterTick(0)
		, category(TechFailureCategory::TRANSIENT)
	{}
};

/**
 * Result of a science candidate attempt.
 */
struct ScienceCandidateResult
{
	bool attempted;           // True if command was attempted
	bool shouldContinue;      // True if should continue to next candidate
	std::string candidateName; // Name of science candidate
	std::string resultReason;  // Result reason (success or failure)

	ScienceCandidateResult()
		: attempted(false)
		, shouldContinue(true)
	{}
};

/**
 * Result of an upgrade candidate attempt.
 */
struct UpgradeCandidateResult
{
	bool attempted;            // True if command was attempted
	bool shouldContinue;       // True if should continue to next candidate
	std::string candidateName; // Name of upgrade candidate
	std::string producerKind;  // Producer kind used
	std::string resultReason;  // Result reason (success or failure)

	UpgradeCandidateResult()
		: attempted(false)
		, shouldContinue(true)
	{}
};

/**
 * Technology manager for autonomous science and upgrade planning.
 *
 * Manages tech candidate selection, failure memory, and retry logic.
 * Extracted from AIControlAdapter main loop to provide clear tech ownership.
 */
class AIControlAdapterTechManager
{
public:
	AIControlAdapterTechManager();

	/**
	 * Attempt to purchase science from the candidate plan.
	 *
	 * Scans science candidates in priority order and attempts the first eligible one.
	 * Continues scanning after candidate-specific failures (e.g., science_not_purchasable).
	 *
	 * @param sciencePlan Array of science names in priority order
	 * @param sciencePlanSize Size of science plan array
	 * @param hasScienceEconomy True if prerequisites for economy sciences are met
	 * @param hasScienceAdvanced True if prerequisites for advanced sciences are met
	 * @param money Current cash balance
	 * @param tryCommandFunc Function to attempt a command (returns true if command issued)
	 * @param currentTick Current game tick for failure memory
	 * @return Result with attempt status and reason
	 */
	ScienceCandidateResult ChooseAndAttemptScience(
		const char* const* sciencePlan,
		int sciencePlanSize,
		bool hasScienceEconomy,
		bool hasScienceAdvanced,
		unsigned int money,
		std::function<bool(const char* category, const char* command, const char* scienceName, std::string& reason)> tryCommandFunc,
		unsigned int currentTick);

	/**
	 * Attempt to purchase upgrade from the candidate plan.
	 *
	 * Scans upgrade candidates in priority order and attempts the first eligible one.
	 * Continues scanning after producer-specific failures (e.g., producer_cannot_make_upgrade).
	 *
	 * @param upgradePlan Array of {producerKind, upgradeName} pairs
	 * @param upgradePlanSize Size of upgrade plan array
	 * @param playerHasUpgradeCompleteFunc Function to check if upgrade is already owned
	 * @param producerCounts Map of producer kind -> count (e.g., "black_market" -> 2)
	 * @param money Current cash balance
	 * @param tryCommandFunc Function to attempt a command (returns true if command issued)
	 * @param currentTick Current game tick for failure memory
	 * @return Result with attempt status and reason
	 */
	UpgradeCandidateResult ChooseAndAttemptUpgrade(
		const void* upgradePlan,
		int upgradePlanSize,
		std::function<bool(const char* upgradeName)> playerHasUpgradeCompleteFunc,
		const std::map<std::string, int>& producerCounts,
		unsigned int money,
		std::function<bool(const char* category, const char* command, const char* producerKind, const char* upgradeName, std::string& reason)> tryCommandFunc,
		unsigned int currentTick);

	/**
	 * Reset failure memory (called when starting new game).
	 */
	void Reset();

	/**
	 * Get failure memory for debugging/telemetry.
	 */
	const std::map<std::string, TechFailureMemory>& GetFailureMemory() const
	{
		return m_failureMemory;
	}

private:
	/**
	 * Categorize a failure reason for retry logic.
	 */
	TechFailureCategory CategorizeFailureReason(const char* reason, bool isScience) const;

	/**
	 * Record a failure in memory.
	 */
	void RecordFailure(
		const std::string& candidateName,
		const std::string& producerKind,
		const std::string& reason,
		unsigned int currentTick);

	/**
	 * Check if a candidate/producer combination should be skipped this tick.
	 */
	bool ShouldSkipCandidate(
		const std::string& candidateName,
		const std::string& producerKind,
		unsigned int currentTick) const;

	/**
	 * Build a unique key for failure memory.
	 */
	std::string BuildMemoryKey(const std::string& candidateName, const std::string& producerKind) const;

	// Failure memory: key = "candidate:producer" (or "candidate:" for science)
	std::map<std::string, TechFailureMemory> m_failureMemory;
};
