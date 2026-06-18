#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTemplateInferenceService.h"

#include <algorithm>
#include <cctype>

bool AIControlAdapterTemplateInferenceService::ContainsAsciiLower(std::string haystack, const char* needle)
{
	if (needle == nullptr || *needle == '\0')
	{
		return false;
	}

	std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::string n = needle;
	std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return haystack.find(n) != std::string::npos;
}

std::string AIControlAdapterTemplateInferenceService::InferScudLauncherTemplateForSide(
	const std::string& side,
	const std::string& baseSide)
{
	if (ContainsAsciiLower(side, "chem") || ContainsAsciiLower(baseSide, "chem"))
	{
		return "Chem_GLAVehicleScudLauncher";
	}
	if (ContainsAsciiLower(side, "stealth") || ContainsAsciiLower(baseSide, "stealth") ||
		ContainsAsciiLower(side, "slth") || ContainsAsciiLower(baseSide, "slth"))
	{
		return "Slth_GLAVehicleScudLauncher";
	}
	if (ContainsAsciiLower(side, "demo") || ContainsAsciiLower(baseSide, "demo"))
	{
		return "Demo_GLAVehicleScudLauncher";
	}
	return "GLAVehicleScudLauncher";
}
