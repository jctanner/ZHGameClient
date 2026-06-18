#pragma once

#include <string>

class Object;
class Player;

class AIControlAdapterTemplateInferenceService
{
public:
	static bool ContainsAsciiLower(std::string haystack, const char* needle);

	static std::string InferScudLauncherTemplateForSide(
		const std::string& side,
		const std::string& baseSide);

	static std::string InferScudLauncherTemplate(const Player* player);
	static std::string InferQuadTemplateForProducer(Object* producer);
	static std::string InferScorpionTemplateForProducer(Object* producer);
	static std::string InferRocketBuggyTemplateForProducer(Object* producer);
	static std::string InferTechnicalTemplateForProducer(Object* producer);
};
