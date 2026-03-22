		bool executeGameScudStormAtPosition(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr || TheActionManager == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}
			if (!canIssuePlayerScopedMessage(player, reason))
			{
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto xIt = argsIt->find("x");
			const auto yIt = argsIt->find("y");
			if (xIt == argsIt->end() || yIt == argsIt->end() || !xIt->is_number() || !yIt->is_number())
			{
				reason = "missing_target_position";
				return false;
			}

			Coord3D target;
			target.x = xIt->get<Real>();
			target.y = yIt->get<Real>();
			target.z = 0.0f;

			const SpecialPowerTemplate* powerTemplate = nullptr;
			Object* source = resolveScudStormSourceFromArgs(player, message, powerTemplate, reason);
			if (source == nullptr || powerTemplate == nullptr)
			{
				return false;
			}

			if (!TheActionManager->canDoSpecialPowerAtLocation(source, &target, CMD_FROM_PLAYER, powerTemplate, nullptr, 0u))
			{
				reason = "target_not_valid_for_scud_storm";
				return false;
			}

			GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION);
			if (msg == nullptr)
			{
				reason = "message_stream_not_ready";
				return false;
			}
			msg->appendIntegerArgument(static_cast<Int>(powerTemplate->getID()));
			msg->appendLocationArgument(target);
			msg->appendRealArgument(INVALID_ANGLE);
			msg->appendObjectIDArgument(INVALID_ID);
			msg->appendIntegerArgument(0);
			msg->appendObjectIDArgument(source->getID());
			return true;
		}

		bool executeGameScudStormAtPlayer(const nlohmann::json& message, std::string& reason)
		{
			if (ThePlayerList == nullptr)
			{
				reason = "player_state_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto targetIt = argsIt->find("target_player_index");
			if (targetIt == argsIt->end() || !targetIt->is_number_integer())
			{
				reason = "missing_target_player_index";
				return false;
			}

			Player* targetPlayer = getPlayerByIndex(targetIt->get<Int>());
			if (targetPlayer == nullptr)
			{
				reason = "target_player_not_found";
				return false;
			}

			Coord3D target;
			if (!resolveScudStormTargetPlayerPosition(targetPlayer, target, reason))
			{
				return false;
			}

			nlohmann::json forwarded = message;
			forwarded["args"]["x"] = target.x;
			forwarded["args"]["y"] = target.y;
			return executeGameScudStormAtPosition(forwarded, reason);
		}

		bool executeGameAttackMove(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto xIt = argsIt->find("x");
			const auto yIt = argsIt->find("y");
			if (xIt == argsIt->end() || yIt == argsIt->end() || !xIt->is_number() || !yIt->is_number())
			{
				reason = "missing_target_position";
				return false;
			}

			Coord3D target;
			target.x = xIt->get<Real>();
			target.y = yIt->get<Real>();
			target.z = 0.0f;

			std::vector<Int> objectIds;
			const auto objectIdsIt = argsIt->find("object_ids");
			if (objectIdsIt != argsIt->end() && objectIdsIt->is_array())
			{
				for (const auto& idNode : *objectIdsIt)
				{
					if (idNode.is_number_integer())
					{
						const Int id = idNode.get<Int>();
						if (id > 0)
						{
							objectIds.push_back(id);
						}
					}
				}
			}
			if (objectIds.empty())
			{
				const auto objectIdIt = argsIt->find("object_id");
				if (objectIdIt != argsIt->end() && objectIdIt->is_number_integer())
				{
					const Int id = objectIdIt->get<Int>();
					if (id > 0)
					{
						objectIds.push_back(id);
					}
				}
			}
			if (objectIds.empty())
			{
				reason = "missing_object_ids";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			std::vector<ObjectID> selectedIds;
			for (Int id : objectIds)
			{
				Object* obj = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				if (obj == nullptr || obj->isEffectivelyDead())
				{
					continue;
				}
				if (obj->getControllingPlayer() != player)
				{
					continue;
				}
				if (obj->getAI() == nullptr)
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

		bool executeGameCaptureBuilding(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr || TheActionManager == nullptr)
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
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto targetIdIt = argsIt->find("target_object_id");
			if (targetIdIt == argsIt->end() || !targetIdIt->is_number_integer())
			{
				reason = "missing_target_object_id";
				return false;
			}

			const Int targetId = targetIdIt->get<Int>();
			if (targetId <= 0)
			{
				reason = "invalid_target_object_id";
				return false;
			}

			Object* target = TheGameLogic->findObjectByID(static_cast<ObjectID>(targetId));
			if (target == nullptr)
			{
				reason = "target_not_found";
				return false;
			}

			Object* source = nullptr;
			const auto sourceIdIt = argsIt->find("source_object_id");
			if (sourceIdIt != argsIt->end() && sourceIdIt->is_number_integer())
			{
				const Int sourceId = sourceIdIt->get<Int>();
				if (sourceId <= 0)
				{
					reason = "invalid_source_object_id";
					return false;
				}
				source = TheGameLogic->findObjectByID(static_cast<ObjectID>(sourceId));
				if (source == nullptr)
				{
					reason = "source_not_found";
					return false;
				}
				if (source->getControllingPlayer() != player)
				{
					reason = "source_not_owned";
					return false;
				}
			}
			else
			{
				struct CaptureSourceSearchContext
				{
					Object* found;
					Object* target;
				} ctx = { nullptr, target };

				player->iterateObjects([](Object* obj, void* userData)
				{
					if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
					{
						return;
					}
					CaptureSourceSearchContext* ctx = static_cast<CaptureSourceSearchContext*>(userData);
					if (ctx->found != nullptr)
					{
						return;
					}
					if (!obj->hasSpecialPower(SPECIAL_INFANTRY_CAPTURE_BUILDING) && !obj->hasSpecialPower(SPECIAL_BLACKLOTUS_CAPTURE_BUILDING))
					{
						return;
					}
					const AIUpdateInterface* ai = obj->getAI();
					if (ai != nullptr && !ai->isIdle())
					{
						return;
					}
					if (!TheActionManager->canCaptureBuilding(obj, ctx->target, CMD_FROM_PLAYER))
					{
						return;
					}
					ctx->found = obj;
				}, &ctx);

				source = ctx.found;
			}

			if (source == nullptr)
			{
				reason = "capture_source_not_found";
				return false;
			}
			if (!TheActionManager->canCaptureBuilding(source, target, CMD_FROM_PLAYER))
			{
				reason = "target_not_capturable";
				return false;
			}

			SpecialPowerType powerType = SPECIAL_INFANTRY_CAPTURE_BUILDING;
			SpecialPowerModuleInterface* spInterface = source->findSpecialPowerModuleInterface(powerType);
			if (spInterface == nullptr)
			{
				powerType = SPECIAL_BLACKLOTUS_CAPTURE_BUILDING;
				spInterface = source->findSpecialPowerModuleInterface(powerType);
			}
			if (spInterface == nullptr)
			{
				reason = "capture_power_not_found";
				return false;
			}

			const char* powerTemplateName = (powerType == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING)
				? "SpecialAbilityBlackLotusCaptureBuilding"
				: "SpecialAbilityRebelCaptureBuilding";
			const SpecialPowerTemplate* powerTemplate = TheSpecialPowerStore != nullptr
				? TheSpecialPowerStore->findSpecialPowerTemplate(powerTemplateName)
				: nullptr;
			if (powerTemplate == nullptr)
			{
				reason = "capture_power_template_not_found";
				return false;
			}
			if (!TheActionManager->canDoSpecialPowerAtObject(source, target, CMD_FROM_PLAYER, powerTemplate, 0u))
			{
				reason = "capture_not_ready";
				return false;
			}

			GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT);
			if (msg == nullptr)
			{
				reason = "message_stream_not_ready";
				return false;
			}
			msg->appendIntegerArgument(static_cast<Int>(powerTemplate->getID()));
			msg->appendObjectIDArgument(target->getID());
			msg->appendIntegerArgument(0);
			msg->appendObjectIDArgument(source->getID());
			return true;
		}

		bool executeGameAttackMoveRaidSmart(const nlohmann::json& message, std::string& reason)
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
			Int minUnits = 20;
			Int groupSize = 20;
			Real distance = 3000.0f;
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto minUnitsIt = argsIt->find("min_units");
				if (minUnitsIt != argsIt->end() && minUnitsIt->is_number_integer())
				{
					minUnits = std::max<Int>(1, minUnitsIt->get<Int>());
				}
				const auto groupSizeIt = argsIt->find("group_size");
				if (groupSizeIt != argsIt->end() && groupSizeIt->is_number_integer())
				{
					groupSize = std::max<Int>(1, groupSizeIt->get<Int>());
				}
				const auto distanceIt = argsIt->find("distance");
				if (distanceIt != argsIt->end() && distanceIt->is_number())
				{
					distance = std::max<Real>(256.0f, distanceIt->get<Real>());
				}
			}

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
				const ThingTemplate* tt = obj->getTemplate();
				const std::string name = tt != nullptr ? tt->getName().str() : "";
				// Keep radar coverage at home; do not include radar-type units in raid groups.
				if (containsIgnoreCase(name, "radar"))
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

			if (static_cast<Int>(collectCtx.units.size()) < minUnits)
			{
				reason = "insufficient_combat_units";
				return false;
			}

			Coord3D anchor;
			anchor.x = 0.0f;
			anchor.y = 0.0f;
			anchor.z = 0.0f;
			bool hasAnchor = false;

			Object* cc = findPrimaryCommandCenter(player);
			if (cc != nullptr && cc->getPosition() != nullptr)
			{
				anchor = *cc->getPosition();
				hasAnchor = true;
			}
			if (!hasAnchor)
			{
				nlohmann::json playerPos = buildPlayerMapPositionSummary(player);
				const auto pxIt = playerPos.find("x");
				const auto pyIt = playerPos.find("y");
				if (pxIt != playerPos.end() && pyIt != playerPos.end() && pxIt->is_number() && pyIt->is_number())
				{
					anchor.x = pxIt->get<Real>();
					anchor.y = pyIt->get<Real>();
					anchor.z = 0.0f;
					hasAnchor = true;
				}
			}
			if (!hasAnchor)
			{
				reason = "anchor_position_unknown";
				return false;
			}

			Coord3D target;
			target.z = 0.0f;
			bool haveTarget = false;

			// Prefer random enemy start/base position so raids fan out across opponents.
			std::vector<Coord3D> enemyTargets;
			const Int playerCount = ThePlayerList->getPlayerCount();
			Player* neutral = ThePlayerList->getNeutralPlayer();
			for (Int i = 0; i < playerCount; ++i)
			{
				Player* candidate = ThePlayerList->getNthPlayer(i);
				if (candidate == nullptr || candidate == player || candidate == neutral)
				{
					continue;
				}
				nlohmann::json enemyPos = buildPlayerMapPositionSummary(candidate);
				const auto exIt = enemyPos.find("x");
				const auto eyIt = enemyPos.find("y");
				if (exIt == enemyPos.end() || eyIt == enemyPos.end() || !exIt->is_number() || !eyIt->is_number())
				{
					continue;
				}
				Coord3D p;
				p.x = exIt->get<Real>();
				p.y = eyIt->get<Real>();
				p.z = 0.0f;
				enemyTargets.push_back(p);
			}
			if (!enemyTargets.empty())
			{
				const std::size_t pick = static_cast<std::size_t>(::GetTickCount() % enemyTargets.size());
				target = enemyTargets[pick];
				haveTarget = true;
			}

			// Fallback: directional offset from our anchor.
			if (!haveTarget)
			{
				Int directionIndex = static_cast<Int>(::GetTickCount() & 3u);
				if (argsIt != message.end() && argsIt->is_object())
				{
					const auto directionIt = argsIt->find("direction_index");
					if (directionIt != argsIt->end() && directionIt->is_number_integer())
					{
						const Int requested = directionIt->get<Int>();
						if (requested >= 0)
						{
							directionIndex = requested % 4;
						}
					}
				}

				Real dx = 0.0f;
				Real dy = 0.0f;
				if (directionIndex == 0)
				{
					dx = 1.0f;
				}
				else if (directionIndex == 1)
				{
					dx = -1.0f;
				}
				else if (directionIndex == 2)
				{
					dy = 1.0f;
				}
				else
				{
					dy = -1.0f;
				}
				target.x = anchor.x + (dx * distance);
				target.y = anchor.y + (dy * distance);
			}

			std::vector<ObjectID> selectedIds;
			const std::size_t maxToCommand = std::min<std::size_t>(collectCtx.units.size(), static_cast<std::size_t>(groupSize));
			for (std::size_t i = 0; i < maxToCommand; ++i)
			{
				Object* obj = collectCtx.units[i];
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

		bool executeGameGuardAllIdleGroundCombat(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			struct GuardCommand
			{
				ObjectID id;
				Coord3D pos;
			};
			struct GuardCollectContext
			{
				std::vector<GuardCommand> commands;
			};

			GuardCollectContext collectCtx;
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
				if (!obj->isKindOf(KINDOF_INFANTRY) && !obj->isKindOf(KINDOF_VEHICLE))
				{
					return;
				}
				if (!obj->isAbleToAttack() || obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					return;
				}

				AIUpdateInterface* ai = obj->getAI();
				if (ai == nullptr || !ai->isIdle() || ai->isMoving())
				{
					return;
				}

				const Coord3D* pos = obj->getPosition();
				if (pos == nullptr)
				{
					return;
				}

				GuardCollectContext* ctx = static_cast<GuardCollectContext*>(userData);
				GuardCommand command = {};
				command.id = obj->getID();
				command.pos = *pos;
				command.pos.z = 0.0f;
				ctx->commands.push_back(command);
			}, &collectCtx);

			if (collectCtx.commands.empty())
			{
				reason = "no_valid_objects";
				return false;
			}

			if (!canIssuePlayerScopedMessage(player, reason))
			{
				return false;
			}

			const std::vector<ObjectID> priorSelection = getCurrentSelectionObjectIds(player);
			for (std::vector<GuardCommand>::const_iterator it = collectCtx.commands.begin(); it != collectCtx.commands.end(); ++it)
			{
				appendSelectionMessage(player, std::vector<ObjectID>(1, it->id), true);
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_GUARD_POSITION);
				if (msg == nullptr)
				{
					appendSelectionMessage(player, priorSelection, true);
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendLocationArgument(it->pos);
				msg->appendIntegerArgument(static_cast<Int>(GUARDMODE_NORMAL));
			}
			appendSelectionMessage(player, priorSelection, true);
			return true;
		}

