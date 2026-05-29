		// Phase 9.0: Result structure for attack commands that need to create combat tasks
		struct AttackCommandResult
		{
			bool success = false;
			std::vector<unsigned int> assignedUnitIds;
			Coord3D targetPosition = {0.0f, 0.0f, 0.0f};
			std::string failureReason;
		};

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
			const bool ok = executeScopedSelectionCommand(player, selectedIds, reason, [&]() -> bool
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
			if (ok)
			{
				recordAutonomyTelemetryEvent("attack", "Game.AttackMove.RaidSmart", "ok", &target);
			}
			return ok;
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

		// Phase 9.0: Overload that returns attack result with unit IDs and target for combat task creation
		bool executeGameAttackMoveRaidSmart(const nlohmann::json& message, std::string& reason, AttackCommandResult* outResult = nullptr)
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

			std::vector<Object*> combatUnits;
			collectCombatUnitsForRaid(player, combatUnits);

			if (static_cast<Int>(combatUnits.size()) < minUnits)
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
			const std::size_t maxToCommand = std::min<std::size_t>(combatUnits.size(), static_cast<std::size_t>(groupSize));
			for (std::size_t i = 0; i < maxToCommand; ++i)
			{
				Object* obj = combatUnits[i];
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

			const bool commandSuccess = executeScopedSelectionCommand(player, selectedIds, reason, [&]() -> bool
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

			// Phase 9.0: Populate result structure for combat task creation
			if (outResult != nullptr)
			{
				outResult->success = commandSuccess;
				outResult->targetPosition = target;
				if (commandSuccess)
				{
					outResult->assignedUnitIds.reserve(selectedIds.size());
					for (std::size_t i = 0; i < selectedIds.size(); ++i)
					{
						outResult->assignedUnitIds.push_back(static_cast<unsigned int>(selectedIds[i]));
					}
				}
				else
				{
					outResult->failureReason = reason;
				}
			}

			return commandSuccess;
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
				// Phase 6.2: Pass task reservation manager to filter capture-reserved units
				const AIControlAdapterTaskReservationManager* taskReservationManager;
				AIControlAdapterState* adapter;
				std::vector<ObjectID> skippedReservedUnits; // Track skipped for logging
				std::vector<ObjectID> skippedCombatReservedUnits;
				std::vector<ObjectID> skippedGarrisonReservedUnits;
			};

			GuardCollectContext collectCtx;
			collectCtx.taskReservationManager = &m_autonomy.taskReservationManager;
			collectCtx.adapter = this;
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

				// Phase 6.2: Exclude units reserved for capture tasks
				GuardCollectContext* ctx = static_cast<GuardCollectContext*>(userData);
				if (ctx->taskReservationManager != nullptr &&
					ctx->taskReservationManager->isObjectReserved(obj->getID()))
				{
					ctx->skippedReservedUnits.push_back(obj->getID());
					return;
				}
				if (ctx->adapter != nullptr &&
					ctx->adapter->m_autonomy.combatTaskManager.isUnitReserved(obj->getID()))
				{
					ctx->skippedCombatReservedUnits.push_back(obj->getID());
					return;
				}
				if (ctx->adapter != nullptr &&
					ctx->adapter->isGarrisonReservedUnit(static_cast<UnsignedInt>(obj->getID())))
				{
					ctx->skippedGarrisonReservedUnits.push_back(obj->getID());
					return;
				}

				GuardCommand command = {};
				command.id = obj->getID();
				command.pos = *pos;
				command.pos.z = 0.0f;
				ctx->commands.push_back(command);
			}, &collectCtx);

			// Phase 6.2: Log skipped capture-reserved units
			for (ObjectID skippedId : collectCtx.skippedReservedUnits)
			{
				std::vector<SpecialTaskReservation*> captureTasks = m_autonomy.taskReservationManager.findCaptureTasks();
				for (const SpecialTaskReservation* task : captureTasks)
				{
					if (task != nullptr &&
						task->state != SpecialTaskState::Complete &&
						task->state != SpecialTaskState::Failed &&
						task->state != SpecialTaskState::Expired &&
						task->sourceObjectId == static_cast<unsigned int>(skippedId))
					{
						adapterLog(
							"guard_skip_reserved_capture unit=%u task=%u owner=%s target=%u",
							static_cast<unsigned int>(skippedId),
							task->taskId,
							task->owner.c_str(),
							task->targetObjectId);
						break;
					}
				}
			}
			for (ObjectID skippedId : collectCtx.skippedCombatReservedUnits)
			{
				adapterLog(
					"guard_skip_reserved_combat unit=%u reason=combat_task",
					static_cast<unsigned int>(skippedId));
			}
			for (ObjectID skippedId : collectCtx.skippedGarrisonReservedUnits)
			{
				adapterLog(
					"guard_skip_reserved_garrison unit=%u reason=garrison_assignment",
					static_cast<unsigned int>(skippedId));
			}

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

		// Phase 9.0: Overload that returns defense result with unit IDs for combat task creation
		bool executeGameAttackMoveDefendZoneSmart(const nlohmann::json& message, std::string& reason, AttackCommandResult* outResult = nullptr)
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

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto targetXIt = argsIt->find("target_x");
			const auto targetYIt = argsIt->find("target_y");
			if (targetXIt == argsIt->end() || targetYIt == argsIt->end() || !targetXIt->is_number() || !targetYIt->is_number())
			{
				reason = "missing_target_position";
				return false;
			}

			Coord3D target;
			target.x = targetXIt->get<Real>();
			target.y = targetYIt->get<Real>();
			target.z = 0.0f;

			// Collect idle ground combat units (reuse raid collection logic)
			std::vector<Object*> combatUnits;
			collectCombatUnitsForRaid(player, combatUnits);

			if (combatUnits.empty())
			{
				reason = "no_idle_combat_units";
				return false;
			}

			// Phase 7.4: WMD-aware defense discipline - cap unit count when WMD threat exists
			const bool hasWMDThreat = m_autonomy.wmdTargetTracker.hasActiveWMDThreat();
			std::size_t maxDefenseUnits = combatUnits.size(); // Default: use all units
			const auto maxUnitsIt = argsIt->find("max_units");
			if (maxUnitsIt != argsIt->end() && maxUnitsIt->is_number_integer())
			{
				const Int requestedMaxUnits = maxUnitsIt->get<Int>();
				if (requestedMaxUnits > 0)
				{
					maxDefenseUnits = std::min<std::size_t>(maxDefenseUnits, static_cast<std::size_t>(requestedMaxUnits));
				}
			}

			if (hasWMDThreat)
			{
				// Cap defense groups to prevent mass clustering under WMD threat
				// Smaller groups reduce nuke/particle cannon damage potential
				const auto zoneAnchorIt = argsIt->find("zone_anchor");
				const bool isMainBaseZone = false; // Could check if zone anchor is Command Center/Supply Center

				if (!isMainBaseZone)
				{
					// Non-main-base zones: strict cap
					maxDefenseUnits = std::min(static_cast<std::size_t>(30), combatUnits.size());
				}
				else
				{
					// Main base: more generous cap but still bounded
					maxDefenseUnits = std::min(static_cast<std::size_t>(50), combatUnits.size());
				}

				if (maxDefenseUnits < combatUnits.size())
				{
					adapterLog(
						"zone_defense_cap_applied zone=%u reason=wmd_threat requested=%d capped=%d main_base=%d",
						zoneAnchorIt != argsIt->end() && zoneAnchorIt->is_number()
							? zoneAnchorIt->get<unsigned int>() : 0u,
						static_cast<int>(combatUnits.size()),
						static_cast<int>(maxDefenseUnits),
						isMainBaseZone ? 1 : 0);
				}
			}

			// Select units for defense (capped by WMD discipline if applicable)
			std::vector<ObjectID> selectedIds;
			for (std::size_t i = 0; i < combatUnits.size() && selectedIds.size() < maxDefenseUnits; ++i)
			{
				Object* obj = combatUnits[i];
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

			const bool commandSuccess = executeScopedSelectionCommand(player, selectedIds, reason, [&]() -> bool
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

			// Phase 9.0: Populate result structure for combat task creation
			if (outResult != nullptr)
			{
				outResult->success = commandSuccess;
				outResult->targetPosition = target;
				if (commandSuccess)
				{
					outResult->assignedUnitIds.reserve(selectedIds.size());
					for (std::size_t i = 0; i < selectedIds.size(); ++i)
					{
						outResult->assignedUnitIds.push_back(static_cast<unsigned int>(selectedIds[i]));
					}
				}
				else
				{
					outResult->failureReason = reason;
				}
			}

			return commandSuccess;
		}
