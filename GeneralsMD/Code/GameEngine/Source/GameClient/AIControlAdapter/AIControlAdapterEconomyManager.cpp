/**
 * AIControlAdapterEconomyManager.cpp
 *
 * Implementation of economy recovery and income scaling decision logic.
 */

#include "GameClient/AIControlAdapter/AIControlAdapterEconomyManager.h"
#include <algorithm>

EconomyPolicy AIControlAdapterEconomyManager::AssessEconomyPolicy(const EconomyManagerInput& input) const
{
	EconomyPolicy policy;

	// Classify reserve state first (needed for income classification)
	policy.reserveState = ClassifyReserveState(input.currentMoney, input.reserveCash);

	// Classify income state with hysteresis
	policy.incomeState = ClassifyIncomeState(input.smoothedNetCashPerMinute, input.currentMoney, input.reserveCash, input.inProgressBlackMarkets > 0, policy.reserveState, input.completedBlackMarkets);

	// Determine combat spending mode
	policy.combatSpendingMode = DetermineCombatSpendingMode(policy.incomeState, policy.reserveState);

	// Calculate income targets
	policy.targetIncomePerMinute = CalculateTargetIncome(input.profile);
	policy.sufficientIncomePerMinute = CalculateSufficientIncome(policy.targetIncomePerMinute);

	// Calculate Black Market targets and scaling parameters
	policy.maxBlackMarkets = CalculateMaxBlackMarkets(input.profile);
	policy.softRecoveryMarkets = CalculateSoftRecoveryMarkets(input.profile);
	policy.baselineScalingMarkets = CalculateBaselineScalingMarkets(input.profile);
	policy.maxInProgressMarkets = CalculateMaxInProgressMarkets(policy.reserveState, input.currentMoney, input.completedBlackMarkets);

	// Calculate economy pressure and scaling mode
	policy.economyPressure = CalculateEconomyPressure(input, policy.reserveState, input.completedBlackMarkets);
	policy.scalingMode = DetermineScalingMode(
		policy.incomeState,
		policy.reserveState,
		policy.economyPressure,
		input.completedBlackMarkets,
		input.inProgressBlackMarkets,
		policy.softRecoveryMarkets,
		policy.maxBlackMarkets);

	policy.desiredBlackMarkets = CalculateDesiredBlackMarkets(
		policy.incomeState,
		input.completedBlackMarkets,
		input.inProgressBlackMarkets,
		policy.maxBlackMarkets);

	// Determine recovery priorities
	const bool incomeNeedsRecovery = (policy.incomeState == EconomyIncomeState::Weak ||
									  policy.incomeState == EconomyIncomeState::Critical ||
									  policy.incomeState == EconomyIncomeState::Recovering);

	const bool reserveAllowsEmergency = (policy.reserveState == EconomyReserveState::Pressured ||
										 policy.reserveState == EconomyReserveState::Depleted);

	// In Recovery mode, prioritize and allow emergency builds
	// In Growth mode, continue at normal priority
	policy.prioritizeIncomeBuild = (policy.scalingMode == EconomyScalingMode::Recovery) && incomeNeedsRecovery && reserveAllowsEmergency;
	policy.allowEmergencyIncomeBuild = (policy.scalingMode == EconomyScalingMode::Recovery) && incomeNeedsRecovery && reserveAllowsEmergency;

	// Generate reason
	if (policy.incomeState == EconomyIncomeState::Healthy && policy.reserveState == EconomyReserveState::Protected)
	{
		policy.reason = "income_sufficient";
	}
	else if (policy.incomeState == EconomyIncomeState::Recovering)
	{
		policy.reason = "income_build_in_progress";
	}
	else if (policy.incomeState == EconomyIncomeState::Critical && policy.reserveState == EconomyReserveState::Depleted)
	{
		policy.reason = "reserve_depleted_income_critical";
	}
	else if (policy.incomeState == EconomyIncomeState::Critical && policy.reserveState == EconomyReserveState::Pressured)
	{
		policy.reason = "reserve_pressured_income_critical";
	}
	else if (policy.incomeState == EconomyIncomeState::Weak && policy.reserveState == EconomyReserveState::Pressured)
	{
		policy.reason = "reserve_pressured_income_weak";
	}
	else if (policy.incomeState == EconomyIncomeState::Weak)
	{
		policy.reason = "income_below_target";
	}
	else
	{
		policy.reason = "reserve_protected";
	}

	// Generate scaling reason (Phase 5.6)
	const unsigned int totalMarkets = input.completedBlackMarkets + input.inProgressBlackMarkets;
	if (policy.scalingMode == EconomyScalingMode::Saturated)
	{
		if (totalMarkets >= policy.maxBlackMarkets)
		{
			policy.scalingReason = "hard_cap_reached";
		}
		else
		{
			policy.scalingReason = "saturated_unknown";
		}
	}
	else if (policy.scalingMode == EconomyScalingMode::Recovery)
	{
		if (policy.economyPressure >= 40)
		{
			policy.scalingReason = "high_pressure";
		}
		else if (policy.incomeState == EconomyIncomeState::Critical)
		{
			policy.scalingReason = "critical_income";
		}
		else if (policy.reserveState == EconomyReserveState::Depleted)
		{
			policy.scalingReason = "depleted_reserve";
		}
		else
		{
			policy.scalingReason = "recovery_mode";
		}
	}
	else // Growth
	{
		if (totalMarkets < policy.baselineScalingMarkets)
		{
			policy.scalingReason = "below_baseline";
		}
		else if (policy.economyPressure > 10)
		{
			policy.scalingReason = "positive_pressure";
		}
		else
		{
			policy.scalingReason = "normal_growth";
		}
	}

	return policy;
}

EconomyRecoveryRequest AIControlAdapterEconomyManager::ChooseRecoveryAction(
	const EconomyManagerInput& input,
	const EconomyPolicy& policy) const
{
	EconomyRecoveryRequest request;

	request.completedMarkets = input.completedBlackMarkets;
	request.inProgressMarkets = input.inProgressBlackMarkets;
	request.desiredMarkets = policy.desiredBlackMarkets;
	request.maxMarkets = policy.maxBlackMarkets;

	// Prerequisites check
	if (!input.hasPalace)
	{
		request.shouldBuildIncome = false;
		request.reason = "missing_palace";
		return request;
	}

	// Cap check
	const unsigned int totalMarkets = input.completedBlackMarkets + input.inProgressBlackMarkets;
	if (totalMarkets >= policy.maxBlackMarkets)
	{
		request.shouldBuildIncome = false;
		request.reason = "black_market_cap_reached";
		return request;
	}

	// Income sufficient check (Phase 5.6: only stop in Growth mode if we've reached baseline)
	if (policy.incomeState == EconomyIncomeState::Healthy &&
	    policy.scalingMode != EconomyScalingMode::Growth)
	{
		request.shouldBuildIncome = false;
		request.reason = "income_sufficient";
		return request;
	}

	// In Growth mode, continue scaling even if income is temporarily healthy
	// Only stop for concrete blockers (cap, prerequisites, money, placement, concurrency)
	if (policy.scalingMode == EconomyScalingMode::Growth &&
	    totalMarkets >= policy.baselineScalingMarkets &&
	    policy.economyPressure <= 10)
	{
		// Very low pressure and above baseline: ease off but don't stop completely
		// We'll still allow growth if there's any residual pressure
		request.shouldBuildIncome = false;
		request.reason = "growth_easing_low_pressure";
		return request;
	}

	// In-progress concurrency limit check (Phase 5.6 continuous scaling)
	if (input.inProgressBlackMarkets >= policy.maxInProgressMarkets)
	{
		request.shouldBuildIncome = false;
		request.reason = "in_progress_limit_reached";
		return request;
	}

	// Money check (but allow emergency spending if policy allows)
	const unsigned int blackMarketCost = 1500u; // GLA Black Market cost
	if (input.currentMoney < blackMarketCost)
	{
		request.shouldBuildIncome = false;
		request.reason = "no_money";
		return request;
	}

	// If we reach here, income build is needed and allowed
	request.shouldBuildIncome = true;
	request.buildingCommand = "Game.BuildBlackMarketSmart";
	request.buildingTemplate = "GLABlackMarket";

	// Distinguish recovery (high priority) from growth (normal priority)
	if (policy.scalingMode == EconomyScalingMode::Recovery)
	{
		if (policy.incomeState == EconomyIncomeState::Critical && policy.reserveState != EconomyReserveState::Protected)
		{
			request.reason = "recovery_high_priority_reserve_critical";
		}
		else
		{
			request.reason = "recovery_high_priority";
		}
	}
	else if (policy.scalingMode == EconomyScalingMode::Growth)
	{
		if (input.completedBlackMarkets < policy.baselineScalingMarkets)
		{
			request.reason = "growth_normal_priority_baseline";
		}
		else
		{
			request.reason = "growth_normal_priority";
		}
	}
	else
	{
		request.reason = "build_requested";
	}

	return request;
}

EconomyIncomeState AIControlAdapterEconomyManager::ClassifyIncomeState(
	int smoothedNetIncome,
	unsigned int currentMoney,
	unsigned int reserveCash,
	bool hasInProgressMarkets,
	EconomyReserveState reserveState,
	unsigned int completedMarkets) const
{
	// If income build is already in progress, mark as recovering
	if (hasInProgressMarkets)
	{
		// Stay in recovering until income is positive again
		if (smoothedNetIncome <= 0)
		{
			return EconomyIncomeState::Recovering;
		}

		// Don't exit recovery while reserve is still depleted
		// This prevents temporary income bursts (stash workers returning) from stopping Black Market recovery
		if (reserveState == EconomyReserveState::Depleted)
		{
			return EconomyIncomeState::Recovering;
		}

		// Income is positive and reserve is recovering, check if healthy now
		if (static_cast<unsigned int>(smoothedNetIncome) >= 600u)
		{
			return EconomyIncomeState::Healthy;
		}
		return EconomyIncomeState::Recovering;
	}

	// Critical: zero or negative income, or very low income while reserve is pressured
	if (smoothedNetIncome <= 0)
	{
		return EconomyIncomeState::Critical;
	}

	if (smoothedNetIncome < 300 && currentMoney < reserveCash)
	{
		return EconomyIncomeState::Critical;
	}

	// Weak: positive but below target
	if (static_cast<unsigned int>(smoothedNetIncome) < 600u)
	{
		return EconomyIncomeState::Weak;
	}

	// Even if current income is high, don't classify as fully healthy while reserve is depleted
	// This prevents temporary stash-worker income bursts from stopping Black Market recovery
	if (reserveState == EconomyReserveState::Depleted)
	{
		return EconomyIncomeState::Weak;
	}

	// If income is high but we have few income buildings, it may be a temporary burst
	// (e.g., stash workers returning). Keep in Weak state while reserve is still pressured.
	// Each Black Market provides ~225/min sustained income, so >800/min with <2 markets is suspicious.
	if (completedMarkets < 2u &&
	    static_cast<unsigned int>(smoothedNetIncome) > 800u &&
	    reserveState == EconomyReserveState::Pressured)
	{
		return EconomyIncomeState::Weak;
	}

	// Healthy: income is sufficient and reserve is stable
	return EconomyIncomeState::Healthy;
}

EconomyReserveState AIControlAdapterEconomyManager::ClassifyReserveState(
	unsigned int currentMoney,
	unsigned int reserveCash) const
{
	if (currentMoney < reserveCash)
	{
		return EconomyReserveState::Depleted;
	}

	const unsigned int pressureMargin = reserveCash + 1000u;
	if (currentMoney < pressureMargin)
	{
		return EconomyReserveState::Pressured;
	}

	return EconomyReserveState::Protected;
}

CombatSpendingMode AIControlAdapterEconomyManager::DetermineCombatSpendingMode(
	EconomyIncomeState incomeState,
	EconomyReserveState reserveState) const
{
	// Block normal combat production when income is critical and reserve is pressured or depleted
	if (incomeState == EconomyIncomeState::Critical &&
		(reserveState == EconomyReserveState::Pressured || reserveState == EconomyReserveState::Depleted))
	{
		return CombatSpendingMode::BlockedExceptDefense;
	}

	// Conservative mode when income is weak or reserve is pressured (but not both critical)
	if (incomeState == EconomyIncomeState::Weak || reserveState == EconomyReserveState::Pressured)
	{
		return CombatSpendingMode::Conservative;
	}

	// Recovering: allow conservative spending while income build is in progress
	if (incomeState == EconomyIncomeState::Recovering)
	{
		return CombatSpendingMode::Conservative;
	}

	return CombatSpendingMode::Normal;
}

unsigned int AIControlAdapterEconomyManager::CalculateTargetIncome(const std::string& profile) const
{
	// Target income varies by profile
	// These are conservative starting points, can be tuned based on telemetry
	if (profile == "sprawl" || profile == "sprawl_balanced")
	{
		return 800u; // Support macro + production
	}
	else if (profile == "tech")
	{
		return 600u; // Tech builds are less production-heavy
	}
	else
	{
		return 600u; // Default
	}
}

unsigned int AIControlAdapterEconomyManager::CalculateSufficientIncome(unsigned int targetIncome) const
{
	// Sufficient income is 75% of target (allows recovery to ease before fully hitting target)
	return (targetIncome * 3u) / 4u;
}

unsigned int AIControlAdapterEconomyManager::CalculateDesiredBlackMarkets(
	EconomyIncomeState incomeState,
	unsigned int completedMarkets,
	unsigned int inProgressMarkets,
	unsigned int maxMarkets) const
{
	const unsigned int totalMarkets = completedMarkets + inProgressMarkets;

	// During critical income, request +1 more market (up to max)
	if (incomeState == EconomyIncomeState::Critical || incomeState == EconomyIncomeState::Recovering)
	{
		// Never log desired below current total
		return std::max(totalMarkets, std::min(totalMarkets + 1u, maxMarkets));
	}

	// During weak income, maintain current count or request +1 if we have none
	if (incomeState == EconomyIncomeState::Weak)
	{
		if (totalMarkets == 0)
		{
			return 1u;
		}
		// Never log desired below current total
		return std::max(totalMarkets, std::min(totalMarkets + 1u, maxMarkets));
	}

	// Healthy: maintain current count
	return totalMarkets;
}

unsigned int AIControlAdapterEconomyManager::CalculateMaxBlackMarkets(const std::string& profile) const
{
	// Phase 5.6: High practical hard caps for continuous scaling
	if (profile == "sprawl" || profile == "sprawl_balanced")
	{
		return 32u; // High hard cap for long-game sprawl scaling
	}
	else if (profile == "tech")
	{
		return 16u; // Tech profile still needs substantial income for upgrades/science
	}
	else
	{
		return 24u; // Default high cap
	}
}

unsigned int AIControlAdapterEconomyManager::CalculateSoftRecoveryMarkets(const std::string& profile) const
{
	// Soft target where recovery urgency can ease
	if (profile == "sprawl" || profile == "sprawl_balanced")
	{
		return 4u;
	}
	else if (profile == "tech")
	{
		return 3u;
	}
	else
	{
		return 4u;
	}
}

unsigned int AIControlAdapterEconomyManager::CalculateBaselineScalingMarkets(const std::string& profile) const
{
	// Minimum stable late-game economy floor
	if (profile == "sprawl" || profile == "sprawl_balanced")
	{
		return 10u;
	}
	else if (profile == "tech")
	{
		return 6u;
	}
	else
	{
		return 8u;
	}
}

unsigned int AIControlAdapterEconomyManager::CalculateMaxInProgressMarkets(
	EconomyReserveState reserveState,
	unsigned int currentMoney,
	unsigned int completedMarkets) const
{
	// Concurrency limit: allow more simultaneous builds when reserve is protected and cash is high
	if (reserveState == EconomyReserveState::Depleted || reserveState == EconomyReserveState::Pressured)
	{
		return 1u; // Conservative during reserve pressure
	}

	if (currentMoney > 10000u && completedMarkets >= 6u)
	{
		return 3u; // Allow aggressive expansion when cash is floating
	}

	if (currentMoney > 5000u && completedMarkets >= 3u)
	{
		return 2u; // Moderate expansion
	}

	return 1u; // Default conservative concurrency
}

int AIControlAdapterEconomyManager::CalculateEconomyPressure(
	const EconomyManagerInput& input,
	EconomyReserveState reserveState,
	unsigned int completedMarkets) const
{
	int pressure = 0;

	// Reserve pressure
	if (reserveState == EconomyReserveState::Depleted)
	{
		pressure += 50;
	}
	else if (reserveState == EconomyReserveState::Pressured)
	{
		pressure += 25;
	}

	// Income pressure
	if (input.smoothedNetCashPerMinute <= 0)
	{
		pressure += 40;
	}
	else if (input.smoothedNetCashPerMinute < 600)
	{
		pressure += 20;
	}

	// Low durable income (few completed markets)
	const unsigned int baselineMarkets = CalculateBaselineScalingMarkets(input.profile);
	if (completedMarkets < baselineMarkets)
	{
		pressure += static_cast<int>((baselineMarkets - completedMarkets) * 2);
	}

	// Cash starvation (positive income but still low money)
	if (input.currentMoney < 3000u && input.smoothedNetCashPerMinute > 0)
	{
		pressure += 15;
	}

	return pressure;
}

EconomyScalingMode AIControlAdapterEconomyManager::DetermineScalingMode(
	EconomyIncomeState incomeState,
	EconomyReserveState reserveState,
	int economyPressure,
	unsigned int completedMarkets,
	unsigned int inProgressMarkets,
	unsigned int softRecoveryMarkets,
	unsigned int hardMarketCap) const
{
	const unsigned int totalMarkets = completedMarkets + inProgressMarkets;

	// Saturated: hit hard cap
	if (totalMarkets >= hardMarketCap)
	{
		return EconomyScalingMode::Saturated;
	}

	// Recovery: high pressure or critical income/reserve state
	if (economyPressure >= 40 ||
	    (incomeState == EconomyIncomeState::Critical && reserveState != EconomyReserveState::Protected) ||
	    (reserveState == EconomyReserveState::Depleted && completedMarkets < softRecoveryMarkets))
	{
		return EconomyScalingMode::Recovery;
	}

	// Growth: economy is stable enough for normal-priority scaling
	// Continue growth until hard cap or another concrete blocker
	return EconomyScalingMode::Growth;
}
