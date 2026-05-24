/**
 * AIControlAdapterScheduler.cpp
 *
 * Implementation of intent scheduling system for AI Control Adapter.
 *
 * See AIControlAdapterScheduler.h for architecture and design principles.
 */

#include "GameClient/AIControlAdapter/AIControlAdapterScheduler.h"

#include <algorithm>

AIControlAdapterScheduler::AIControlAdapterScheduler()
	: m_config()
{
}

AIControlAdapterScheduler::AIControlAdapterScheduler(const SchedulerConfig& config)
	: m_config(config)
{
}

void AIControlAdapterScheduler::SubmitIntent(const Intent& intent)
{
	m_pendingIntents.push_back(intent);
}

std::vector<IntentResult> AIControlAdapterScheduler::ProcessIntents(unsigned int currentTick)
{
	std::vector<IntentResult> results;

	// Sort intents by priority (higher priority first)
	std::sort(m_pendingIntents.begin(), m_pendingIntents.end(), ComparePriority);

	// Process each intent in priority order
	for (const Intent& intent : m_pendingIntents)
	{
		IntentResult result;
		result.category = intent.category;
		result.priority = intent.priority;
		result.commandName = intent.commandName;
		result.targetName = intent.targetName;
		result.reason = intent.reason;
		result.tick = currentTick;
		result.producerObjectId = intent.producerObjectId;
		result.producerKind = intent.producerKind;

		// Check if category budget allows this intent
		if (!HasBudget(intent.category))
		{
			// Budget exhausted, skip this intent
			result.state = IntentState::SKIPPED;
			result.attempted = false;
			result.issued = false;
			result.resultReason = "category_budget_exhausted";
			results.push_back(result);
			continue;
		}

		// Attempt execution via executeFunc
		result.attempted = true;
		result.state = IntentState::ATTEMPTED;

		if (intent.executeFunc)
		{
			std::string executeReason;
			bool issued = intent.executeFunc(executeReason);
			result.issued = issued;
			result.resultReason = executeReason;
		}
		else
		{
			// No execute function provided
			result.issued = false;
			result.resultReason = "no_execute_function";
		}

		// Consume budget attempt regardless of success/failure
		// This prevents one failing intent from retrying endlessly
		ConsumeBudget(intent.category);

		results.push_back(result);
	}

	// Clear pending intents and reset budgets for next tick
	m_pendingIntents.clear();
	ResetBudgets();

	return results;
}

std::vector<CategoryBudget> AIControlAdapterScheduler::GetCategoryBudgets() const
{
	return m_config.categoryBudgets;
}

void AIControlAdapterScheduler::Reset()
{
	m_pendingIntents.clear();
	ResetBudgets();
}

CategoryBudget* AIControlAdapterScheduler::FindBudget(IntentCategory category)
{
	for (CategoryBudget& budget : m_config.categoryBudgets)
	{
		if (budget.category == category)
		{
			return &budget;
		}
	}
	return nullptr;
}

bool AIControlAdapterScheduler::HasBudget(IntentCategory category)
{
	CategoryBudget* budget = FindBudget(category);
	if (!budget)
	{
		// No budget configured for this category, allow by default
		return true;
	}

	return budget->currentAttempts < budget->maxAttemptsPerTick;
}

void AIControlAdapterScheduler::ConsumeBudget(IntentCategory category)
{
	CategoryBudget* budget = FindBudget(category);
	if (budget)
	{
		budget->currentAttempts++;
	}
}

void AIControlAdapterScheduler::ResetBudgets()
{
	for (CategoryBudget& budget : m_config.categoryBudgets)
	{
		budget.currentAttempts = 0;
	}
}

bool AIControlAdapterScheduler::ComparePriority(const Intent& a, const Intent& b)
{
	// Lower priority enum value = higher priority (CRITICAL=0 > LOW=3)
	return static_cast<int>(a.priority) < static_cast<int>(b.priority);
}
