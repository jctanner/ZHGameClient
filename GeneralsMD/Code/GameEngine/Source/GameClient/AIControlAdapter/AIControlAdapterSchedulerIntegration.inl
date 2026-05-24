/**
 * AIControlAdapterSchedulerIntegration.inl
 *
 * Scheduler integration helpers for autonomy loop.
 *
 * This file provides helper functions to convert manager outputs (ProductionIntent, TechManager results)
 * into scheduler Intents and process the results.
 */

// Helper: Convert intent category to string for logging
static const char* IntentCategoryToString(IntentCategory category)
{
	switch (category)
	{
		case IntentCategory::TECH_SCIENCE: return "tech_science";
		case IntentCategory::TECH_UPGRADE: return "tech_upgrade";
		case IntentCategory::PRODUCTION: return "production";
		case IntentCategory::DEFENSE: return "defense";
		case IntentCategory::DEFENSE_RESPONSE: return "defense_response";
		case IntentCategory::MACRO: return "macro";
		case IntentCategory::ECONOMY: return "economy_recovery";
		case IntentCategory::COMBAT: return "combat";
		default: return "unknown";
	}
}

// Helper: Convert intent priority to string for logging
static const char* IntentPriorityToString(IntentPriority priority)
{
	switch (priority)
	{
		case IntentPriority::CRITICAL: return "critical";
		case IntentPriority::HIGH: return "high";
		case IntentPriority::NORMAL: return "normal";
		case IntentPriority::LOW: return "low";
		default: return "unknown";
	}
}
