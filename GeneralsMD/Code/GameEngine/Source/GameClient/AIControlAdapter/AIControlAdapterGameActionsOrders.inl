		bool executeGameAttackMoveAllCombatToPlayer(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr || ThePlayerList == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			const auto argsIt = message.find("args");
			bool hasTargetPlayerIndex = false;
			Int targetPlayerIndex = -1;
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto targetIt = argsIt->find("target_player_index");
				if (targetIt != argsIt->end() && targetIt->is_number_integer())
				{
					hasTargetPlayerIndex = true;
					targetPlayerIndex = targetIt->get<Int>();
				}
			}

			Player* targetPlayer = nullptr;
			if (hasTargetPlayerIndex)
			{
				targetPlayer = getPlayerByIndex(targetPlayerIndex);
				if (targetPlayer == nullptr)
				{
					reason = "target_player_not_found";
					return false;
				}
			}
			else
			{
				const Int playerCount = ThePlayerList->getPlayerCount();
				Player* neutral = ThePlayerList->getNeutralPlayer();
				for (Int i = 0; i < playerCount; ++i)
				{
					Player* candidate = ThePlayerList->getNthPlayer(i);
					if (candidate == nullptr || candidate == player || candidate == neutral)
					{
						continue;
					}
					targetPlayer = candidate;
					break;
				}
				if (targetPlayer == nullptr)
				{
					reason = "target_player_not_found";
					return false;
				}
			}

			nlohmann::json targetPos = buildPlayerMapPositionSummary(targetPlayer);
			const auto xIt = targetPos.find("x");
			const auto yIt = targetPos.find("y");
			if (xIt == targetPos.end() || yIt == targetPos.end() || !xIt->is_number() || !yIt->is_number())
			{
				reason = "target_position_unknown";
				return false;
			}

			Coord3D target;
			target.x = xIt->get<Real>();
			target.y = yIt->get<Real>();
			target.z = 0.0f;

			struct CombatCollectContext
			{
				std::vector<Object*> units;
			};

			CombatCollectContext collectCtx;
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				if (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_HARVESTER))
				{
					return;
				}
				if (!obj->isKindOf(KINDOF_INFANTRY) && !obj->isKindOf(KINDOF_VEHICLE) && !obj->isKindOf(KINDOF_AIRCRAFT))
				{
					return;
				}
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					return;
				}
				if (obj->getAI() == nullptr)
				{
					return;
				}

				CombatCollectContext* ctx = static_cast<CombatCollectContext*>(userData);
				ctx->units.push_back(obj);
			}, &collectCtx);

			if (collectCtx.units.empty())
			{
				reason = "no_combat_units";
				return false;
			}

			std::vector<ObjectID> selectedIds;
			for (std::vector<Object*>::const_iterator it = collectCtx.units.begin(); it != collectCtx.units.end(); ++it)
			{
				Object* obj = *it;
				if (obj == nullptr || obj->getAI() == nullptr)
				{
					continue;
				}
				selectedIds.push_back(obj->getID());
			}

			if (selectedIds.empty())
			{
				reason = "no_valid_objects";
				return false;
			}
			return executeScopedSelectionCommand(player, selectedIds, reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_ATTACKMOVETO);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendLocationArgument(target);
				return true;
			});
		}
