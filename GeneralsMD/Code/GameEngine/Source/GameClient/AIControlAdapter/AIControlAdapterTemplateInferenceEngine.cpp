#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTemplateInferenceService.h"

#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/Object.h"

#include <vector>

std::string AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(const Player* player)
{
	if (player == nullptr)
	{
		return std::string();
	}

	return InferScudLauncherTemplateForSide(player->getSide().str(), player->getBaseSide().str());
}

namespace
{
	bool isPotentiallyQueueable(Object* producer, const ThingTemplate* thingTemplate)
	{
		if (producer == nullptr || thingTemplate == nullptr || TheBuildAssistant == nullptr)
		{
			return false;
		}

		const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, thingTemplate);
		return canMake == CANMAKE_OK ||
			canMake == CANMAKE_NO_MONEY ||
			canMake == CANMAKE_QUEUE_FULL ||
			canMake == CANMAKE_PARKING_PLACES_FULL;
	}
}

std::string AIControlAdapterTemplateInferenceService::InferQuadTemplateForProducer(Object* producer)
{
	if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
	{
		return std::string();
	}

	std::vector<std::string> candidates;
	const Player* player = producer->getControllingPlayer();
	const std::string side = player != nullptr ? player->getSide().str() : std::string();
	const std::string baseSide = player != nullptr ? player->getBaseSide().str() : std::string();
	if (ContainsAsciiLower(side, "gla") || ContainsAsciiLower(baseSide, "gla"))
	{
		if (ContainsAsciiLower(side, "slth") || ContainsAsciiLower(side, "stealth"))
		{
			candidates.push_back("GC_Slth_GLAVehicleQuadCannon");
		}
		if (ContainsAsciiLower(side, "chem") || ContainsAsciiLower(side, "toxin"))
		{
			candidates.push_back("GC_Chem_GLAVehicleQuadCannon");
		}
		if (ContainsAsciiLower(side, "demo"))
		{
			candidates.push_back("Demo_GLAVehicleQuadCannon");
		}
	}
	candidates.push_back("GLAVehicleQuadCannon");
	candidates.push_back("GLAVehicleQuadcannon");
	candidates.push_back("GLAQuadCannon");
	candidates.push_back("GLAVehicleQuad");
	candidates.push_back("GLAQuad");

	for (const std::string& name : candidates)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name.c_str()), false);
		if (isPotentiallyQueueable(producer, tt))
		{
			return name;
		}
	}

	for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
	{
		const std::string templateName = tt->getName().str();
		if (!ContainsAsciiLower(templateName, "quad"))
		{
			continue;
		}
		if (!ContainsAsciiLower(templateName, "vehicle") && !ContainsAsciiLower(templateName, "cannon"))
		{
			continue;
		}
		if (isPotentiallyQueueable(producer, tt))
		{
			return templateName;
		}
	}
	return std::string();
}

std::string AIControlAdapterTemplateInferenceService::InferScorpionTemplateForProducer(Object* producer)
{
	if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
	{
		return std::string();
	}

	const char* candidates[] = {
		"GLAVehicleScorpion",
		"GLAVehicleScorpionTank",
		"GLATankScorpion"
	};
	for (const char* name : candidates)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
		if (tt != nullptr && TheBuildAssistant->canMakeUnit(producer, tt) == CANMAKE_OK)
		{
			return name;
		}
	}
	return std::string();
}

std::string AIControlAdapterTemplateInferenceService::InferRocketBuggyTemplateForProducer(Object* producer)
{
	if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
	{
		return std::string();
	}

	const char* candidates[] = {
		"GLAVehicleRocketBuggy"
	};
	for (const char* name : candidates)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
		if (isPotentiallyQueueable(producer, tt))
		{
			return name;
		}
	}
	for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
	{
		const std::string templateName = tt->getName().str();
		if (ContainsAsciiLower(templateName, "rocketbuggy") && isPotentiallyQueueable(producer, tt))
		{
			return templateName;
		}
	}
	return std::string();
}

std::string AIControlAdapterTemplateInferenceService::InferTechnicalTemplateForProducer(Object* producer)
{
	if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
	{
		return std::string();
	}

	const char* candidates[] = {
		"GLAVehicleTechnical"
	};
	for (const char* name : candidates)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
		if (isPotentiallyQueueable(producer, tt))
		{
			return name;
		}
	}
	for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
	{
		const std::string templateName = tt->getName().str();
		if (ContainsAsciiLower(templateName, "technical") && isPotentiallyQueueable(producer, tt))
		{
			return templateName;
		}
	}
	return std::string();
}
