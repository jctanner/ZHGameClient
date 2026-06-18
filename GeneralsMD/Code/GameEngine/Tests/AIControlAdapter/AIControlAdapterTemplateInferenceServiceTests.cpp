#include "GameClient/AIControlAdapter/AIControlAdapterTemplateInferenceService.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << "\n";
			std::exit(1);
		}
	}

	void expectEq(const std::string& actual, const std::string& expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << "\n";
			std::exit(1);
		}
	}
}

int main()
{
	expect(AIControlAdapterTemplateInferenceService::ContainsAsciiLower("GLA_StealthGeneral", "stealth"),
		"ContainsAsciiLower should match ignoring case");
	expect(!AIControlAdapterTemplateInferenceService::ContainsAsciiLower("GLA", ""),
		"ContainsAsciiLower should reject empty needle");
	expect(!AIControlAdapterTemplateInferenceService::ContainsAsciiLower("GLA", nullptr),
		"ContainsAsciiLower should reject null needle");

	expectEq(
		AIControlAdapterTemplateInferenceService::InferScudLauncherTemplateForSide("GLA", "GLA"),
		"GLAVehicleScudLauncher",
		"Default GLA should use baseline SCUD launcher");
	expectEq(
		AIControlAdapterTemplateInferenceService::InferScudLauncherTemplateForSide("GLA_StealthGeneral", "GLA"),
		"Slth_GLAVehicleScudLauncher",
		"Stealth GLA should use stealth SCUD launcher");
	expectEq(
		AIControlAdapterTemplateInferenceService::InferScudLauncherTemplateForSide("GLA_SlthGeneral", "GLA"),
		"Slth_GLAVehicleScudLauncher",
		"Slth side alias should use stealth SCUD launcher");
	expectEq(
		AIControlAdapterTemplateInferenceService::InferScudLauncherTemplateForSide("GLA_ToxinGeneral", "GLA_Chem"),
		"Chem_GLAVehicleScudLauncher",
		"Chemical base side should use chemical SCUD launcher");
	expectEq(
		AIControlAdapterTemplateInferenceService::InferScudLauncherTemplateForSide("GLA_DemoGeneral", "GLA"),
		"Demo_GLAVehicleScudLauncher",
		"Demo GLA should use demo SCUD launcher");

	std::cout << "All template inference service tests passed!\n";
	return 0;
}
