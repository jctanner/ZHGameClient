#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterTemplateInferenceService.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/Object.h"
#include "GameClient/ControlBar.h"

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

namespace
{
	bool matchesUnitRole(const std::string& name, const std::string& role)
	{
		auto contains = [&](const char* token) { return AIControlAdapterTemplateInferenceService::ContainsAsciiLower(name, token); };
		if (role == "worker") return contains("worker") || contains("dozer");
		if (role == "soldier") return contains("rebel") || contains("redguard") || contains("ranger") || contains("minigunner");
		if (role == "rpg") return contains("tunneldefender") || contains("rpg");
		if (role == "radarvan") return contains("radarvan") || name == "GLAVehicleRadar";
		return AIControlAdapterTemplateInferenceService::ContainsAsciiLower(name, role.c_str());
	}
}

std::string AIControlAdapterTemplateInferenceService::InferUnitTemplateForProducer(Object* producer, const char* role)
{
	if (producer == nullptr || role == nullptr || TheBuildAssistant == nullptr || TheControlBar == nullptr) return std::string();
	const CommandSet* commands = TheControlBar->findCommandSet(producer->getCommandSetString());
	if (commands != nullptr)
	{
		for (Int index = 0; index < MAX_COMMANDS_PER_SET; ++index)
		{
			const CommandButton* button = commands->getCommandButton(index);
			if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD) continue;
			const ThingTemplate* thing = button->getThingTemplate();
			if (thing != nullptr && matchesUnitRole(thing->getName().str(), role) && isPotentiallyQueueable(producer, thing))
			{
				return thing->getName().str();
			}
		}
		return std::string();
	}

	return std::string();
}

std::string AIControlAdapterTemplateInferenceService::InferQuadTemplateForProducer(Object* producer)
{
	return InferUnitTemplateForProducer(producer, "quad");
}

std::string AIControlAdapterTemplateInferenceService::InferScorpionTemplateForProducer(Object* producer)
{
	return InferUnitTemplateForProducer(producer, "scorpion");
}

std::string AIControlAdapterTemplateInferenceService::InferRocketBuggyTemplateForProducer(Object* producer)
{
	return InferUnitTemplateForProducer(producer, "rocketbuggy");
}

std::string AIControlAdapterTemplateInferenceService::InferTechnicalTemplateForProducer(Object* producer)
{
	return InferUnitTemplateForProducer(producer, "technical");
}
