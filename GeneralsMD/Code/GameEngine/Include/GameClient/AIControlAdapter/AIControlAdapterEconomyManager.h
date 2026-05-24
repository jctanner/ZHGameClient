/**
 * AIControlAdapterEconomyManager.h
 *
 * Economy recovery and income scaling decision logic for AIControlAdapter autonomy.
 *
 * Responsibilities:
 * - Detect weak/negative income and reserve pressure
 * - Emit economy policy (spending mode, income targets, recovery priorities)
 * - Request emergency Black Market construction when income is below target
 * - Suppress normal combat production during critical income/reserve pressure
 * - Allow emergency income builds to spend protected reserve when they are the recovery path
 */

#pragma once

#include <string>

/**
 * Income health state based on smoothed net cash per minute and reserve pressure.
 */
enum class EconomyIncomeState
{
	Healthy,     // Income is sufficient, no recovery needed
	Weak,        // Income is positive but below target
	Critical,    // Income is zero or negative, or very low while reserve is pressured
	Recovering   // Income build is in progress, hold other spending
};

/**
 * Reserve pressure state based on current money vs. configured reserve threshold.
 */
enum class EconomyReserveState
{
	Protected,  // Money is above reserve threshold
	Pressured,  // Money is near or at reserve threshold
	Depleted    // Money is below reserve threshold
};

/**
 * Combat spending mode that production/scheduler should honor.
 */
enum class CombatSpendingMode
{
	Normal,              // Normal combat production allowed
	Conservative,        // Reduce combat production pressure
	BlockedExceptDefense // Block normal combat production, allow defense only
};

/**
 * Economy scaling mode for continuous growth vs emergency recovery.
 */
enum class EconomyScalingMode
{
	Recovery,   // Reserve/income is in trouble; build income at high priority
	Growth,     // Economy is stable but should keep scaling at normal priority
	Saturated   // Hard cap or placement/cash constraints block further scaling
};

/**
 * Economy policy emitted by EconomyManager for consumption by ProductionManager and Scheduler.
 */
struct EconomyPolicy
{
	EconomyIncomeState incomeState = EconomyIncomeState::Healthy;
	EconomyReserveState reserveState = EconomyReserveState::Protected;
	CombatSpendingMode combatSpendingMode = CombatSpendingMode::Normal;
	EconomyScalingMode scalingMode = EconomyScalingMode::Growth;

	bool prioritizeIncomeBuild = false;      // Scheduler should prefer income recovery over normal production
	bool allowEmergencyIncomeBuild = false;  // MacroBuild can spend protected reserve for recovery

	unsigned int targetIncomePerMinute = 0;      // Target income for normal operations
	unsigned int sufficientIncomePerMinute = 0;  // Income level where recovery can ease

	unsigned int desiredBlackMarkets = 0;        // Desired Black Market count for current income state
	unsigned int maxBlackMarkets = 0;            // Maximum Black Markets to build (hard cap)
	unsigned int softRecoveryMarkets = 0;        // Markets needed before recovery urgency eases
	unsigned int baselineScalingMarkets = 0;     // Minimum stable late-game economy floor
	unsigned int maxInProgressMarkets = 1;       // Concurrency throttle for simultaneous builds

	int economyPressure = 0;  // Aggregate reason to keep scaling (0 = no pressure, higher = more pressure)

	std::string reason;  // Human-readable reason for current policy
	std::string scalingReason;  // Reason for current scaling mode
};

/**
 * Emergency income build request (e.g., Black Market construction during reserve recovery).
 */
struct EconomyRecoveryRequest
{
	bool shouldBuildIncome = false;

	std::string buildingCommand;   // e.g., "Game.BuildBlackMarketSmart"
	std::string buildingTemplate;  // e.g., "GLABlackMarket"
	std::string reason;            // e.g., "reserve_recovery_income_build"

	unsigned int completedMarkets = 0;
	unsigned int inProgressMarkets = 0;
	unsigned int desiredMarkets = 0;
	unsigned int maxMarkets = 0;
};

/**
 * Input snapshot for EconomyManager decisions.
 */
struct EconomyManagerInput
{
	unsigned int currentMoney = 0;
	int smoothedNetCashPerMinute = 0;  // Can be negative

	unsigned int reserveCash = 0;  // Configured reserve threshold

	unsigned int completedBlackMarkets = 0;
	unsigned int inProgressBlackMarkets = 0;  // Being built or queued

	bool hasPalace = false;  // GLA Palace prerequisite for Black Markets

	std::string profile;  // e.g., "sprawl", "sprawl_balanced", "tech"
};

/**
 * EconomyManager: Owns income health assessment and emergency income recovery decisions.
 *
 * Does NOT own:
 * - Worker production (remains with existing worker logic)
 * - Combat production (ProductionManager)
 * - Actual macro build execution (MacroBuildManager + existing build helpers)
 *
 * Does own:
 * - Income state classification (healthy, weak, critical, recovering)
 * - Reserve pressure detection
 * - Combat spending mode (normal, conservative, blocked except defense)
 * - Emergency Black Market recovery requests
 */
class AIControlAdapterEconomyManager
{
public:
	AIControlAdapterEconomyManager() = default;

	/**
	 * Assess economy health and emit policy.
	 *
	 * @param input Current game state snapshot
	 * @return EconomyPolicy with income/reserve state and spending mode
	 */
	EconomyPolicy AssessEconomyPolicy(const EconomyManagerInput& input) const;

	/**
	 * Determine if emergency Black Market construction is needed and allowed.
	 *
	 * @param input Current game state snapshot
	 * @param policy Current economy policy
	 * @return EconomyRecoveryRequest with build decision
	 */
	EconomyRecoveryRequest ChooseRecoveryAction(const EconomyManagerInput& input, const EconomyPolicy& policy) const;

private:
	/**
	 * Classify income state based on smoothed net income and reserve pressure.
	 * Includes hysteresis to prevent temporary income bursts from stopping recovery prematurely.
	 */
	EconomyIncomeState ClassifyIncomeState(int smoothedNetIncome, unsigned int currentMoney, unsigned int reserveCash, bool hasInProgressMarkets, EconomyReserveState reserveState, unsigned int completedMarkets) const;

	/**
	 * Classify reserve state based on current money vs. reserve threshold.
	 */
	EconomyReserveState ClassifyReserveState(unsigned int currentMoney, unsigned int reserveCash) const;

	/**
	 * Determine combat spending mode based on income and reserve state.
	 */
	CombatSpendingMode DetermineCombatSpendingMode(EconomyIncomeState incomeState, EconomyReserveState reserveState) const;

	/**
	 * Calculate target income per minute based on profile and game stage.
	 */
	unsigned int CalculateTargetIncome(const std::string& profile) const;

	/**
	 * Calculate sufficient income (recovery threshold) based on target.
	 */
	unsigned int CalculateSufficientIncome(unsigned int targetIncome) const;

	/**
	 * Calculate desired Black Market count based on income state.
	 */
	unsigned int CalculateDesiredBlackMarkets(EconomyIncomeState incomeState, unsigned int completedMarkets, unsigned int inProgressMarkets, unsigned int maxMarkets) const;

	/**
	 * Calculate maximum Black Markets based on profile.
	 */
	unsigned int CalculateMaxBlackMarkets(const std::string& profile) const;

	/**
	 * Calculate soft recovery target (markets needed before recovery urgency eases).
	 */
	unsigned int CalculateSoftRecoveryMarkets(const std::string& profile) const;

	/**
	 * Calculate baseline scaling target (minimum stable late-game economy floor).
	 */
	unsigned int CalculateBaselineScalingMarkets(const std::string& profile) const;

	/**
	 * Calculate maximum in-progress markets (concurrency limit).
	 */
	unsigned int CalculateMaxInProgressMarkets(EconomyReserveState reserveState, unsigned int currentMoney, unsigned int completedMarkets) const;

	/**
	 * Calculate economy pressure (aggregate reason to keep scaling).
	 */
	int CalculateEconomyPressure(const EconomyManagerInput& input, EconomyReserveState reserveState, unsigned int completedMarkets) const;

	/**
	 * Determine scaling mode (Recovery, Growth, or Saturated).
	 */
	EconomyScalingMode DetermineScalingMode(EconomyIncomeState incomeState, EconomyReserveState reserveState, int economyPressure, unsigned int completedMarkets, unsigned int inProgressMarkets, unsigned int softRecoveryMarkets, unsigned int hardMarketCap) const;
};
