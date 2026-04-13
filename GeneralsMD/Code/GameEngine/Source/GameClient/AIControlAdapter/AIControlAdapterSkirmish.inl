		static const char* slotStateToString(SlotState state)
		{
			switch (state)
			{
			case SLOT_OPEN: return "open";
			case SLOT_CLOSED: return "closed";
			case SLOT_EASY_AI: return "easy_ai";
			case SLOT_MED_AI: return "med_ai";
			case SLOT_BRUTAL_AI: return "brutal_ai";
			case SLOT_PLAYER: return "human";
			default: return "unknown";
			}
		}

		static SlotState stringToSlotState(const std::string& stateStr)
		{
			if (stateStr == "open") return SLOT_OPEN;
			if (stateStr == "closed") return SLOT_CLOSED;
			if (stateStr == "easy_ai" || stateStr == "easy") return SLOT_EASY_AI;
			if (stateStr == "med_ai" || stateStr == "medium" || stateStr == "med") return SLOT_MED_AI;
			if (stateStr == "brutal_ai" || stateStr == "brutal" || stateStr == "hard") return SLOT_BRUTAL_AI;
			if (stateStr == "human" || stateStr == "player") return SLOT_PLAYER;
			return SLOT_OPEN;
		}

		nlohmann::json buildSkirmishSlotInfo(const GameSlot* slot, Int slotIndex) const
		{
			if (slot == nullptr)
			{
				return nlohmann::json{
					{"slot_index", slotIndex},
					{"state", "unknown"},
					{"color", -1},
					{"template", -1},
					{"team", -1},
					{"start_position", -1},
					{"name", ""}
				};
			}

			return nlohmann::json{
				{"slot_index", slotIndex},
				{"state", slotStateToString(slot->getState())},
				{"color", slot->getColor()},
				{"template", slot->getPlayerTemplate()},
				{"team", slot->getTeamNumber()},
				{"start_position", slot->getStartPos()},
				{"name", unicodeToUtf8(slot->getName())}
			};
		}

		nlohmann::json buildSkirmishSetup() const
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				return nlohmann::json{
					{"available", false},
					{"path", "game.skirmish_setup"}
				};
			}

			nlohmann::json slots = nlohmann::json::array();
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				const GameSlot* slot = TheSkirmishGameInfo->getConstSlot(i);
				slots.push_back(buildSkirmishSlotInfo(slot, i));
			}

			const Money& startingCash = TheSkirmishGameInfo->getStartingCash();

			return nlohmann::json{
				{"available", true},
				{"map", TheSkirmishGameInfo->getMap().str()},
				{"seed", TheSkirmishGameInfo->getSeed()},
				{"starting_cash", startingCash.countMoney()},
				{"superweapon_restricted", TheSkirmishGameInfo->getSuperweaponRestriction() != 0u},
				{"superweapon_restriction_mask", TheSkirmishGameInfo->getSuperweaponRestriction()},
				{"local_slot_num", TheSkirmishGameInfo->getLocalSlotNum()},
				{"is_host", TheSkirmishGameInfo->amIHost()},
				{"slots", slots},
				{"path", "game.skirmish_setup"}
			};
		}

		bool executeSkirmishSetSlot(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			Int slotIndex = -1;
			const auto slotIt = argsIt->find("slot_index");
			if (slotIt == argsIt->end() || !slotIt->is_number_integer())
			{
				const auto slotAlt = argsIt->find("slot");
				if (slotAlt != argsIt->end() && slotAlt->is_number_integer())
				{
					slotIndex = slotAlt->get<Int>();
				}
				else
				{
					reason = "missing_slot_index";
					return false;
				}
			}
			else
			{
				slotIndex = slotIt->get<Int>();
			}

			if (slotIndex < 0 || slotIndex >= MAX_SLOTS)
			{
				reason = "invalid_slot_index";
				return false;
			}

			GameSlot* slot = TheSkirmishGameInfo->getSlot(slotIndex);
			if (slot == nullptr)
			{
				reason = "slot_not_found";
				return false;
			}

			const auto stateIt = argsIt->find("state");
			if (stateIt != argsIt->end() && stateIt->is_string())
			{
				const std::string stateStr = stateIt->get<std::string>();
				const SlotState newState = stringToSlotState(stateStr);
				slot->setState(newState);
			}

			const auto colorIt = argsIt->find("color");
			if (colorIt != argsIt->end() && colorIt->is_number_integer())
			{
				slot->setColor(colorIt->get<Int>());
			}

			const auto templateIt = argsIt->find("template");
			if (templateIt != argsIt->end() && templateIt->is_number_integer())
			{
				slot->setPlayerTemplate(templateIt->get<Int>());
			}

			const auto teamIt = argsIt->find("team");
			if (teamIt != argsIt->end() && teamIt->is_number_integer())
			{
				slot->setTeamNumber(teamIt->get<Int>());
			}

			const auto startPosIt = argsIt->find("start_position");
			if (startPosIt != argsIt->end() && startPosIt->is_number_integer())
			{
				slot->setStartPos(startPosIt->get<Int>());
			}

			const auto nameIt = argsIt->find("name");
			if (nameIt != argsIt->end() && nameIt->is_string())
			{
				const std::string nameStr = nameIt->get<std::string>();
				AsciiString asciiName(nameStr.c_str());
				UnicodeString unicodeName;
				unicodeName.translate(asciiName);
				slot->setName(unicodeName);
			}

			return true;
		}

		bool executeSkirmishSetMap(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string mapName = getJsonString(*argsIt, "map");
			if (mapName.empty())
			{
				reason = "missing_map";
				return false;
			}

			TheSkirmishGameInfo->setMap(AsciiString(mapName.c_str()));
			return true;
		}

		bool executeSkirmishSetStartingCash(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto cashIt = argsIt->find("cash");
			if (cashIt == argsIt->end() || !cashIt->is_number_integer())
			{
				reason = "missing_cash";
				return false;
			}

			const Int cashAmount = cashIt->get<Int>();
			if (cashAmount < 0)
			{
				reason = "invalid_cash_amount";
				return false;
			}

			Money newCash;
			newCash.setMoney(static_cast<UnsignedInt>(cashAmount));
			TheSkirmishGameInfo->setStartingCash(newCash);
			return true;
		}

		bool executeSkirmishSetSuperweaponRestriction(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto restrictedIt = argsIt->find("restricted");
			if (restrictedIt == argsIt->end() || !restrictedIt->is_boolean())
			{
				reason = "missing_restricted";
				return false;
			}

			const bool restricted = restrictedIt->get<bool>();
			TheSkirmishGameInfo->setSuperweaponRestriction(restricted ? 1u : 0u);
			return true;
		}

		bool executeSkirmishSetSeed(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto seedIt = argsIt->find("seed");
			if (seedIt == argsIt->end() || !seedIt->is_number_integer())
			{
				reason = "missing_seed";
				return false;
			}

			const Int seed = seedIt->get<Int>();
			TheSkirmishGameInfo->setSeed(seed);
			return true;
		}

		bool executeSkirmishStart(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			Int gameID = 0;
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto gameIDIt = argsIt->find("game_id");
				if (gameIDIt != argsIt->end() && gameIDIt->is_number_integer())
				{
					gameID = gameIDIt->get<Int>();
				}
			}

			TheSkirmishGameInfo->startGame(gameID);
			return true;
		}

		bool executeSkirmishConfigure(const nlohmann::json& message, std::string& reason)
		{
			if (TheSkirmishGameInfo == nullptr)
			{
				reason = "skirmish_not_available";
				return false;
			}

			if (!TheSkirmishGameInfo->amIHost())
			{
				reason = "not_host";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto mapIt = argsIt->find("map");
			if (mapIt != argsIt->end() && mapIt->is_string())
			{
				TheSkirmishGameInfo->setMap(AsciiString(mapIt->get<std::string>().c_str()));
			}

			const auto cashIt = argsIt->find("starting_cash");
			if (cashIt != argsIt->end() && cashIt->is_number_integer())
			{
				const Int cashAmount = cashIt->get<Int>();
				if (cashAmount >= 0)
				{
					Money newCash;
					newCash.setMoney(static_cast<UnsignedInt>(cashAmount));
					TheSkirmishGameInfo->setStartingCash(newCash);
				}
			}

			const auto swIt = argsIt->find("superweapon_restricted");
			if (swIt != argsIt->end() && swIt->is_boolean())
			{
				TheSkirmishGameInfo->setSuperweaponRestriction(swIt->get<bool>() ? 1u : 0u);
			}

			const auto seedIt = argsIt->find("seed");
			if (seedIt != argsIt->end() && seedIt->is_number_integer())
			{
				TheSkirmishGameInfo->setSeed(seedIt->get<Int>());
			}

			const auto slotsIt = argsIt->find("slots");
			if (slotsIt != argsIt->end() && slotsIt->is_array())
			{
				for (const auto& slotConfig : *slotsIt)
				{
					if (!slotConfig.is_object())
					{
						continue;
					}

					Int slotIndex = -1;
					const auto slotIdxIt = slotConfig.find("slot_index");
					if (slotIdxIt != slotConfig.end() && slotIdxIt->is_number_integer())
					{
						slotIndex = slotIdxIt->get<Int>();
					}
					else
					{
						const auto slotAlt = slotConfig.find("slot");
						if (slotAlt != slotConfig.end() && slotAlt->is_number_integer())
						{
							slotIndex = slotAlt->get<Int>();
						}
					}

					if (slotIndex < 0 || slotIndex >= MAX_SLOTS)
					{
						continue;
					}

					GameSlot* slot = TheSkirmishGameInfo->getSlot(slotIndex);
					if (slot == nullptr)
					{
						continue;
					}

					const auto stateIt = slotConfig.find("state");
					if (stateIt != slotConfig.end() && stateIt->is_string())
					{
						slot->setState(stringToSlotState(stateIt->get<std::string>()));
					}

					const auto colorIt = slotConfig.find("color");
					if (colorIt != slotConfig.end() && colorIt->is_number_integer())
					{
						slot->setColor(colorIt->get<Int>());
					}

					const auto templateIt = slotConfig.find("template");
					if (templateIt != slotConfig.end() && templateIt->is_number_integer())
					{
						slot->setPlayerTemplate(templateIt->get<Int>());
					}

					const auto teamIt = slotConfig.find("team");
					if (teamIt != slotConfig.end() && teamIt->is_number_integer())
					{
						slot->setTeamNumber(teamIt->get<Int>());
					}

					const auto startPosIt = slotConfig.find("start_position");
					if (startPosIt != slotConfig.end() && startPosIt->is_number_integer())
					{
						slot->setStartPos(startPosIt->get<Int>());
					}

					const auto nameIt = slotConfig.find("name");
					if (nameIt != slotConfig.end() && nameIt->is_string())
					{
						AsciiString asciiName(nameIt->get<std::string>().c_str());
						UnicodeString unicodeName;
						unicodeName.translate(asciiName);
						slot->setName(unicodeName);
					}
				}
			}

			return true;
		}
