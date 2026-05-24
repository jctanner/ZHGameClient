/**
 * AIControlAdapterTechManager.cpp
 *
 * Implementation of technology management system.
 *
 * See AIControlAdapterTechManager.h for design principles and architecture.
 */

#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTechManager.h"

#include <cstring>
#include <algorithm>

// Retry delay constants
static const unsigned int kScienceCandidateRetryDelayMs = 10000u;  // 10 seconds between candidate retries
static const unsigned int kUpgradeCandidateRetryDelayMs = 10000u;  // 10 seconds between upgrade retries
static const unsigned int kProducerSpecificRetryDelayMs = 5000u;   // 5 seconds for producer-specific failures
static const unsigned int kTransientRetryDelayMs = 5000u;          // 5 seconds for transient failures

// Helper to convert tick (milliseconds) to retry delay
static unsigned int TicksToMs(unsigned int ticks)
{
	return ticks;  // Assuming ticks are already in milliseconds
}

AIControlAdapterTechManager::AIControlAdapterTechManager()
{
}

void AIControlAdapterTechManager::Reset()
{
	m_failureMemory.clear();
}

TechFailureCategory AIControlAdapterTechManager::CategorizeFailureReason(const char* reason, bool isScience) const
{
	if (reason == nullptr || *reason == '\0')
	{
		return TechFailureCategory::TRANSIENT;
	}

	// Permanent failures - tech not available for this faction
	if (std::strcmp(reason, "science_not_available") == 0 ||
		std::strcmp(reason, "upgrade_not_available") == 0)
	{
		return TechFailureCategory::PERMANENT;
	}

	// Producer-specific failures - this producer can't make it, try others
	if (std::strcmp(reason, "producer_cannot_make_upgrade") == 0 ||
		std::strcmp(reason, "invalid_producer_for_tech") == 0)
	{
		return TechFailureCategory::PRODUCER_SPECIFIC;
	}

	// Candidate-specific failures - skip this candidate for now, try next
	// This is the key fix for the science bug!
	if (std::strcmp(reason, "science_not_purchasable") == 0 ||
		std::strcmp(reason, "science_already_owned") == 0 ||
		std::strcmp(reason, "upgrade_already_complete") == 0 ||
		std::strcmp(reason, "upgrade_already_in_production") == 0)
	{
		return TechFailureCategory::CANDIDATE_SPECIFIC;
	}

	// Transient failures - retry later (no money, queue full, etc.)
	if (std::strcmp(reason, "no_money") == 0 ||
		std::strcmp(reason, "insufficient_resources") == 0 ||
		std::strcmp(reason, "queue_full") == 0 ||
		std::strcmp(reason, "producer_busy") == 0 ||
		std::strcmp(reason, "producer_not_ready") == 0)
	{
		return TechFailureCategory::TRANSIENT;
	}

	// Default to transient (safer to retry than to permanently block)
	return TechFailureCategory::TRANSIENT;
}

std::string AIControlAdapterTechManager::BuildMemoryKey(const std::string& candidateName, const std::string& producerKind) const
{
	// Format: "candidate:producer" or "candidate:" for science
	return candidateName + ":" + producerKind;
}

void AIControlAdapterTechManager::RecordFailure(
	const std::string& candidateName,
	const std::string& producerKind,
	const std::string& reason,
	unsigned int currentTick)
{
	const std::string key = BuildMemoryKey(candidateName, producerKind);
	const TechFailureCategory category = CategorizeFailureReason(reason.c_str(), producerKind.empty());

	auto it = m_failureMemory.find(key);
	if (it == m_failureMemory.end())
	{
		// New failure entry
		TechFailureMemory memory;
		memory.candidateName = candidateName;
		memory.producerKind = producerKind;
		memory.reason = reason;
		memory.category = category;
		memory.firstSeenTick = currentTick;
		memory.lastSeenTick = currentTick;

		// Set retry delay based on category
		switch (category)
		{
		case TechFailureCategory::PERMANENT:
			memory.retryAfterTick = 0xFFFFFFFF;  // Never retry
			break;
		case TechFailureCategory::PRODUCER_SPECIFIC:
			memory.retryAfterTick = currentTick + kProducerSpecificRetryDelayMs;
			break;
		case TechFailureCategory::CANDIDATE_SPECIFIC:
			memory.retryAfterTick = currentTick + kScienceCandidateRetryDelayMs;
			break;
		case TechFailureCategory::TRANSIENT:
			memory.retryAfterTick = currentTick + kTransientRetryDelayMs;
			break;
		}

		m_failureMemory[key] = memory;
	}
	else
	{
		// Update existing failure entry
		TechFailureMemory& memory = it->second;
		memory.lastSeenTick = currentTick;
		memory.reason = reason;  // Update to most recent reason

		// Extend retry delay if category changed to more severe
		const TechFailureCategory newCategory = category;
		if (newCategory != memory.category)
		{
			memory.category = newCategory;
			switch (newCategory)
			{
			case TechFailureCategory::PERMANENT:
				memory.retryAfterTick = 0xFFFFFFFF;
				break;
			case TechFailureCategory::PRODUCER_SPECIFIC:
				memory.retryAfterTick = currentTick + kProducerSpecificRetryDelayMs;
				break;
			case TechFailureCategory::CANDIDATE_SPECIFIC:
				memory.retryAfterTick = currentTick + kScienceCandidateRetryDelayMs;
				break;
			case TechFailureCategory::TRANSIENT:
				memory.retryAfterTick = currentTick + kTransientRetryDelayMs;
				break;
			}
		}
	}
}

bool AIControlAdapterTechManager::ShouldSkipCandidate(
	const std::string& candidateName,
	const std::string& producerKind,
	unsigned int currentTick) const
{
	const std::string key = BuildMemoryKey(candidateName, producerKind);
	const auto it = m_failureMemory.find(key);

	if (it == m_failureMemory.end())
	{
		return false;  // No memory = not failed yet, try it
	}

	const TechFailureMemory& memory = it->second;

	// Permanent failures = always skip
	if (memory.category == TechFailureCategory::PERMANENT)
	{
		return true;
	}

	// Skip if retry delay hasn't elapsed
	if (currentTick < memory.retryAfterTick)
	{
		return true;
	}

	return false;
}

ScienceCandidateResult AIControlAdapterTechManager::ChooseAndAttemptScience(
	const char* const* sciencePlan,
	int sciencePlanSize,
	bool hasScienceEconomy,
	bool hasScienceAdvanced,
	unsigned int money,
	std::function<bool(const char* category, const char* command, const char* scienceName, std::string& reason)> tryCommandFunc,
	unsigned int currentTick)
{
	ScienceCandidateResult result;
	result.attempted = false;
	result.shouldContinue = false;  // Default: don't continue (we'll set true if we should)

	for (int i = 0; i < sciencePlanSize; ++i)
	{
		const std::string scienceName = sciencePlan[i];

		// Note: We don't check prerequisites here - the game's Player::isCapableOfPurchasingScience()
		// will return science_not_purchasable if rank/building requirements aren't met.
		// TechManager categorizes that as CANDIDATE_SPECIFIC and continues scanning.

		// Check failure memory
		if (ShouldSkipCandidate(scienceName, "", currentTick))
		{
			continue;  // Skip but keep scanning
		}

		// Attempt this candidate
		std::string reason;
		const bool issued = tryCommandFunc("auto_tech", "Game.PurchaseScience", scienceName.c_str(), reason);

		result.candidateName = scienceName;
		result.resultReason = reason;

		if (issued)
		{
			// Success!
			result.attempted = true;
			result.shouldContinue = false;  // Stop scanning, we got one
			return result;
		}

		// Failed - categorize and decide whether to continue
		const TechFailureCategory category = CategorizeFailureReason(reason.c_str(), true);

		// Record the failure
		RecordFailure(scienceName, "", reason, currentTick);

		// Decide whether to continue scanning based on failure category
		switch (category)
		{
		case TechFailureCategory::CANDIDATE_SPECIFIC:
			// This candidate can't be purchased now, but others might work
			// This is the KEY FIX - we continue scanning!
			continue;

		case TechFailureCategory::PERMANENT:
			// This science not available, try next
			continue;

		case TechFailureCategory::TRANSIENT:
			// Transient failure (no money, etc.) - stop for this tick
			result.shouldContinue = false;
			return result;

		case TechFailureCategory::PRODUCER_SPECIFIC:
			// Not applicable for science (no producer concept)
			continue;
		}
	}

	// Scanned entire plan, nothing worked
	result.shouldContinue = false;
	return result;
}

UpgradeCandidateResult AIControlAdapterTechManager::ChooseAndAttemptUpgrade(
	const void* upgradePlan,
	int upgradePlanSize,
	std::function<bool(const char* upgradeName)> playerHasUpgradeCompleteFunc,
	const std::map<std::string, int>& producerCounts,
	unsigned int money,
	std::function<bool(const char* category, const char* command, const char* producerKind, const char* upgradeName, std::string& reason)> tryCommandFunc,
	unsigned int currentTick)
{
	UpgradeCandidateResult result;
	result.attempted = false;
	result.shouldContinue = false;

	// Cast upgradePlan to the expected structure type
	struct UpgradePlanEntry
	{
		const char* producerKind;
		const char* upgradeName;
	};
	const UpgradePlanEntry* plan = static_cast<const UpgradePlanEntry*>(upgradePlan);

	for (int i = 0; i < upgradePlanSize; ++i)
	{
		const std::string producerKind = plan[i].producerKind;
		const std::string upgradeName = plan[i].upgradeName;

		// Check if already owned
		if (playerHasUpgradeCompleteFunc(upgradeName.c_str()))
		{
			continue;  // Skip but keep scanning
		}

		// Check producer availability
		auto producerIt = producerCounts.find(producerKind);
		if (producerKind != "any" && (producerIt == producerCounts.end() || producerIt->second < 1))
		{
			continue;  // No producer of this kind, skip but keep scanning
		}

		// Check failure memory
		if (ShouldSkipCandidate(upgradeName, producerKind, currentTick))
		{
			continue;  // Skip but keep scanning
		}

		// Attempt this candidate
		std::string reason;
		const bool issued = tryCommandFunc("auto_tech", "Game.QueueUpgrade", producerKind.c_str(), upgradeName.c_str(), reason);

		result.candidateName = upgradeName;
		result.producerKind = producerKind;
		result.resultReason = reason;

		if (issued)
		{
			// Success!
			result.attempted = true;
			result.shouldContinue = false;  // Stop scanning, we got one
			return result;
		}

		// Failed - categorize and decide whether to continue
		const TechFailureCategory category = CategorizeFailureReason(reason.c_str(), false);

		// Record the failure
		RecordFailure(upgradeName, producerKind, reason, currentTick);

		// Decide whether to continue scanning based on failure category
		switch (category)
		{
		case TechFailureCategory::CANDIDATE_SPECIFIC:
			// This upgrade can't be purchased now, but others might work
			// KEY FIX - continue scanning!
			continue;

		case TechFailureCategory::PRODUCER_SPECIFIC:
			// This producer can't make this upgrade, but another producer might make other upgrades
			// KEY FIX - continue scanning!
			continue;

		case TechFailureCategory::PERMANENT:
			// This upgrade not available for this faction, try next
			continue;

		case TechFailureCategory::TRANSIENT:
			// Transient failure (no money, queue full, etc.) - stop for this tick
			result.shouldContinue = false;
			return result;
		}
	}

	// Scanned entire plan, nothing worked
	result.shouldContinue = false;
	return result;
}
