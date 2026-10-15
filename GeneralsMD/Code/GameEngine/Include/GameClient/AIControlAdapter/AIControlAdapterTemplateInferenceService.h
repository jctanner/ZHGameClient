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
	// Role discovery uses the producer's current command set and engine legality.
	static std::string InferUnitTemplateForProducer(Object* producer, const char* role);
	static std::string InferQuadTemplateForProducer(Object* producer);
	static std::string InferScorpionTemplateForProducer(Object* producer);
	static std::string InferRocketBuggyTemplateForProducer(Object* producer);
	static std::string InferTechnicalTemplateForProducer(Object* producer);
};
