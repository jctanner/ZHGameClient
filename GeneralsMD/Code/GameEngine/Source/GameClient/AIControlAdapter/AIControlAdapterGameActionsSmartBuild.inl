		bool executeGameBuildSupplyStashAuto(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* idleWorker = resolveWorkerFromArgs(player, message, true, reason);
			if (idleWorker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				buildingTemplateName = getJsonString(*argsIt, "building_template");
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferSupplyBuildingTemplateForPlayer(player, idleWorker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "supply_building_template_unknown";
				return false;
			}

			nlohmann::json bridged = message;
			nlohmann::json bridgedArgs = nlohmann::json::object();
			if (argsIt != message.end() && argsIt->is_object())
			{
				bridgedArgs = *argsIt;
			}
			bridgedArgs["building_template"] = buildingTemplateName;
			bridgedArgs["remember_supply_choice"] = true;
			const auto lastSupplyIt = m_lastAutoSupplySourceByPlayer.find(player->getPlayerIndex());
			if (lastSupplyIt != m_lastAutoSupplySourceByPlayer.end() && lastSupplyIt->second > 0)
			{
				bridgedArgs["avoid_supply_source_id"] = lastSupplyIt->second;
			}
			bridgedArgs["worker_object_id"] = static_cast<Int>(idleWorker->getID());
			bridgedArgs["allow_reserved_worker"] = true;
			bridged["args"] = bridgedArgs;

			return executeGameDozerConstruct(bridged, reason);
		}

		bool executeGameBuildSupplyStashSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			std::string buildingTemplateName;
			Int minimumCash = 1;
			Int requestedSupplyId = -1;
			Int avoidSupplyId = -1;
			bool preferRemoteSupply = false;
			Coord3D remoteOrigin = {};
			bool hasRemoteOrigin = false;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto minCashIt = argsIt->find("minimum_cash");
				if (minCashIt != argsIt->end() && minCashIt->is_number_integer())
				{
					minimumCash = minCashIt->get<Int>();
				}
				const auto sourceIdIt = argsIt->find("supply_source_id");
				if (sourceIdIt != argsIt->end() && sourceIdIt->is_number_integer())
				{
					requestedSupplyId = sourceIdIt->get<Int>();
				}
				const auto preferRemoteIt = argsIt->find("prefer_remote");
				if (preferRemoteIt != argsIt->end() && preferRemoteIt->is_boolean())
				{
					preferRemoteSupply = preferRemoteIt->get<bool>();
				}
				const auto remoteOriginIt = argsIt->find("remote_origin");
				if (remoteOriginIt != argsIt->end() && remoteOriginIt->is_object())
				{
					const auto xIt = remoteOriginIt->find("x");
					const auto yIt = remoteOriginIt->find("y");
					if (xIt != remoteOriginIt->end() && xIt->is_number() && yIt != remoteOriginIt->end() && yIt->is_number())
					{
						remoteOrigin.x = xIt->get<Real>();
						remoteOrigin.y = yIt->get<Real>();
						remoteOrigin.z = 0.0f;
						hasRemoteOrigin = true;
					}
				}
			}

			// Preserve legacy auto behavior when no explicit supply source was requested.
			std::string buildReason;
			const std::string requestId = getRequestIdForLog(message);
			if (requestedSupplyId <= 0)
			{
				if (executeGameBuildSupplyStashAuto(message, buildReason))
				{
					adapterLog("supply_stash_smart_auto_success request_id=%s requested_supply=0", requestId.c_str());
					return true;
				}

				// If failure is unrelated to placement/movement legality, surface it as-is.
				if (buildReason != "no_legal_build_location" &&
					buildReason != "construct_failed" &&
					buildReason != "blocked_by_shroud" &&
					buildReason != "blocked_by_objects" &&
					buildReason != "no_clear_path" &&
					buildReason != "too_close_to_supply")
				{
					reason = buildReason;
					return false;
				}
				adapterLog("supply_stash_smart_auto_fallback request_id=%s reason=%s", requestId.c_str(), buildReason.c_str());
			}

			if (TheThingFactory == nullptr || TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			if (requestedSupplyId <= 0)
			{
				const auto lastSupplyIt = m_lastAutoSupplySourceByPlayer.find(player->getPlayerIndex());
				if (lastSupplyIt != m_lastAutoSupplySourceByPlayer.end() && lastSupplyIt->second > 0)
				{
					avoidSupplyId = lastSupplyIt->second;
				}
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferSupplyBuildingTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "supply_building_template_unknown";
				return false;
			}

			std::vector<SupplySourceInfo> sources;
			if (!collectSupplySources(minimumCash, sources))
			{
				reason = "supply_scan_not_ready";
				return false;
			}
			if (sources.empty())
			{
				reason = "supply_source_not_found";
				return false;
			}

			Object* selectedSupply = nullptr;
			if (requestedSupplyId > 0)
			{
				for (const SupplySourceInfo& info : sources)
				{
					if (static_cast<Int>(info.source->getID()) == requestedSupplyId)
					{
						selectedSupply = info.source;
						break;
					}
				}
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}
			else
			{
				if (preferRemoteSupply && hasRemoteOrigin)
				{
					selectedSupply = chooseRemoteSupplySource(sources, &remoteOrigin, player, true, avoidSupplyId);
				}
				if (selectedSupply == nullptr)
				{
					selectedSupply = chooseClosestSupplySource(sources, worker->getPosition(), player, true, avoidSupplyId);
				}
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}
			m_lastAutoSupplySourceByPlayer[player->getPlayerIndex()] = static_cast<Int>(selectedSupply->getID());

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Coord3D location;
			Real angle = 0.0f;
			if (findBuildLocationNearSupply(player, worker, selectedSupply, buildingTemplate, location, angle))
			{
				const Coord3D* workerPos = worker->getPosition();
				const Coord3D* supplyPos = selectedSupply->getPosition();
				adapterLog(
					"supply_stash_smart_construct request_id=%s player=%d worker=%d worker_pos=(%.1f,%.1f) requested_supply=%d selected_supply=%d supply_pos=(%.1f,%.1f) template=%s location=(%.1f,%.1f) angle=%.3f",
					requestId.c_str(),
					static_cast<int>(player->getPlayerIndex()),
					static_cast<int>(worker->getID()),
					workerPos != nullptr ? workerPos->x : 0.0f,
					workerPos != nullptr ? workerPos->y : 0.0f,
					static_cast<int>(requestedSupplyId),
					static_cast<int>(selectedSupply->getID()),
					supplyPos != nullptr ? supplyPos->x : 0.0f,
					supplyPos != nullptr ? supplyPos->y : 0.0f,
					buildingTemplateName.c_str(),
					location.x,
					location.y,
					angle);
				if (preferRemoteSupply)
				{
					adapterLog(
						"supply_stash_remote_selection request_id=%s selected_supply=%d origin=(%.1f,%.1f) supply_pos=(%.1f,%.1f) reason=coverage_footprint",
						requestId.c_str(),
						static_cast<int>(selectedSupply->getID()),
						remoteOrigin.x,
						remoteOrigin.y,
						supplyPos != nullptr ? supplyPos->x : 0.0f,
						supplyPos != nullptr ? supplyPos->y : 0.0f);
				}
				if (requestId.rfind("auto_", 0) == 0)
				{
					recordAutonomyTelemetryEvent("build", "build_supply_stash_construct", "supply_zone", &location);
				}
				return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
			}

			const Coord3D* supplyPos = selectedSupply->getPosition();
			if (supplyPos == nullptr)
			{
				reason = "supply_source_not_found";
				return false;
			}

			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr)
			{
				reason = "worker_no_ai";
				return false;
			}

			// Move toward the supply zone to reveal shroud near likely stash placements.
			Coord3D target = *supplyPos;
			const Coord3D* workerPos = worker->getPosition();
			if (workerPos != nullptr)
			{
				Real dx = supplyPos->x - workerPos->x;
				Real dy = supplyPos->y - workerPos->y;
				const Real lenSq = dx * dx + dy * dy;
				if (lenSq > 1.0f)
				{
					const Real invLen = 1.0f / std::sqrt(lenSq);
					const Real stopDist = selectedSupply->getGeometryInfo().getBoundingCircleRadius() + 140.0f;
					target.x = supplyPos->x - dx * invLen * stopDist;
					target.y = supplyPos->y - dy * invLen * stopDist;
				}
			}
			target.z = 0.0f;
			adapterLog(
				"supply_stash_smart_move request_id=%s player=%d worker=%d requested_supply=%d selected_supply=%d supply_pos=(%.1f,%.1f) move_target=(%.1f,%.1f) template=%s",
				requestId.c_str(),
				static_cast<int>(player->getPlayerIndex()),
				static_cast<int>(worker->getID()),
				static_cast<int>(requestedSupplyId),
				static_cast<int>(selectedSupply->getID()),
				supplyPos != nullptr ? supplyPos->x : 0.0f,
				supplyPos != nullptr ? supplyPos->y : 0.0f,
				target.x,
				target.y,
				buildingTemplateName.c_str());
			if (requestId.rfind("auto_", 0) == 0)
			{
				recordAutonomyTelemetryEvent("build", "build_supply_stash_move", "supply_zone", &target);
			}
			return moveWorkerToPosition(worker, &target, reason);
		}

		bool executeGameBuildBarracksSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int requestedAnchorId = -1;
			ZonePlacementArgs zoneArgs = {};
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				zoneArgs = parseZonePlacementArgs(&(*argsIt));
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferBarracksTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "barracks_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}

			Coord3D location;
			Real angle = 0.0f;
			bool foundLocation = false;
			if (zoneArgs.hasZoneCenter)
			{
				foundLocation = findBuildLocationInZone(player, worker, buildingTemplate, zoneArgs.zoneCenter, zoneArgs.zoneRadius, location, angle);
				if (!foundLocation && zoneArgs.strictZone)
				{
					logSmartBuildPlacement(message, "barracks", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &zoneArgs.zoneCenter, 0.0f, "zone_strict");
					return moveWorkerToPosition(worker, &zoneArgs.zoneCenter, reason);
				}
			}
			if (!foundLocation)
			{
				foundLocation = findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle);
			}
			if (!foundLocation)
			{
				const Coord3D* moveTarget = zoneArgs.hasZoneCenter
					? &zoneArgs.zoneCenter
					: (anchor != nullptr ? anchor->getPosition() : worker->getPosition());
				logSmartBuildPlacement(message, "barracks", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, moveTarget, 0.0f, zoneArgs.hasZoneCenter ? "zone_fallback" : "anchor_fallback");
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

			logSmartBuildPlacement(message, "barracks", "construct", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &location, angle, zoneArgs.hasZoneCenter ? "zone_or_anchor" : "anchor");
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildCommandCenterSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			Int requestedAnchorId = -1;
			std::string buildingTemplateName;
			ZonePlacementArgs zoneArgs = {};
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				zoneArgs = parseZonePlacementArgs(&(*argsIt));
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferCommandCenterTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "command_center_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}
			if (anchor == nullptr && !zoneArgs.hasZoneCenter)
			{
				reason = "anchor_not_found";
				return false;
			}

			Coord3D location;
			Real angle = 0.0f;
			bool foundLocation = false;
			if (zoneArgs.hasZoneCenter)
			{
				foundLocation = findBuildLocationInZone(player, worker, buildingTemplate, zoneArgs.zoneCenter, zoneArgs.zoneRadius, location, angle);
				if (!foundLocation && zoneArgs.strictZone)
				{
					logSmartBuildPlacement(message, "command_center", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &zoneArgs.zoneCenter, 0.0f, "zone_strict");
					return moveWorkerToPosition(worker, &zoneArgs.zoneCenter, reason);
				}
			}
			if (!foundLocation)
			{
				foundLocation = findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle);
			}
			if (!foundLocation)
			{
				const Coord3D* moveTarget = zoneArgs.hasZoneCenter
					? &zoneArgs.zoneCenter
					: (anchor != nullptr ? anchor->getPosition() : worker->getPosition());
				logSmartBuildPlacement(message, "command_center", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, moveTarget, 0.0f, zoneArgs.hasZoneCenter ? "zone_fallback" : "anchor_fallback");
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

			logSmartBuildPlacement(message, "command_center", "construct", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &location, angle, zoneArgs.hasZoneCenter ? "zone_or_anchor" : "anchor");
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildArmsDealerSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int requestedAnchorId = -1;
			ZonePlacementArgs zoneArgs = {};
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				zoneArgs = parseZonePlacementArgs(&(*argsIt));
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferArmsDealerTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "arms_dealer_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}

			Coord3D location;
			Real angle = 0.0f;
			bool foundLocation = false;
			if (zoneArgs.hasZoneCenter)
			{
				foundLocation = findBuildLocationInZone(player, worker, buildingTemplate, zoneArgs.zoneCenter, zoneArgs.zoneRadius, location, angle);
				if (!foundLocation && zoneArgs.strictZone)
				{
					logSmartBuildPlacement(message, "arms_dealer", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &zoneArgs.zoneCenter, 0.0f, "zone_strict");
					return moveWorkerToPosition(worker, &zoneArgs.zoneCenter, reason);
				}
			}
			if (!foundLocation)
			{
				foundLocation = findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle);
			}
			if (!foundLocation)
			{
				const Coord3D* moveTarget = zoneArgs.hasZoneCenter
					? &zoneArgs.zoneCenter
					: (anchor != nullptr ? anchor->getPosition() : worker->getPosition());
				logSmartBuildPlacement(message, "arms_dealer", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, moveTarget, 0.0f, zoneArgs.hasZoneCenter ? "zone_fallback" : "anchor_fallback");
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

			logSmartBuildPlacement(message, "arms_dealer", "construct", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &location, angle, zoneArgs.hasZoneCenter ? "zone_or_anchor" : "anchor");
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildPalaceSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int requestedAnchorId = -1;
			ZonePlacementArgs zoneArgs = {};
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				zoneArgs = parseZonePlacementArgs(&(*argsIt));
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferPalaceTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "palace_template_unknown";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}

			Coord3D location;
			Real angle = 0.0f;
			bool foundLocation = false;
			if (zoneArgs.hasZoneCenter)
			{
				foundLocation = findBuildLocationInZone(player, worker, buildingTemplate, zoneArgs.zoneCenter, zoneArgs.zoneRadius, location, angle);
				if (!foundLocation && zoneArgs.strictZone)
				{
					logSmartBuildPlacement(message, "palace", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &zoneArgs.zoneCenter, 0.0f, "zone_strict");
					return moveWorkerToPosition(worker, &zoneArgs.zoneCenter, reason);
				}
			}
			if (!foundLocation)
			{
				foundLocation = findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle);
			}
			if (!foundLocation)
			{
				const Coord3D* moveTarget = zoneArgs.hasZoneCenter
					? &zoneArgs.zoneCenter
					: (anchor != nullptr ? anchor->getPosition() : worker->getPosition());
				logSmartBuildPlacement(message, "palace", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, moveTarget, 0.0f, zoneArgs.hasZoneCenter ? "zone_fallback" : "anchor_fallback");
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

			logSmartBuildPlacement(message, "palace", "construct", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &location, angle, zoneArgs.hasZoneCenter ? "zone_or_anchor" : "anchor");
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildBlackMarketSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int requestedAnchorId = -1;
			ZonePlacementArgs zoneArgs = {};
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				zoneArgs = parseZonePlacementArgs(&(*argsIt));
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferBlackMarketTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "black_market_prereq_missing";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}

			Coord3D location;
			Real angle = 0.0f;
			bool foundLocation = false;
			if (zoneArgs.hasZoneCenter)
			{
				foundLocation = findBuildLocationInZone(player, worker, buildingTemplate, zoneArgs.zoneCenter, zoneArgs.zoneRadius, location, angle);
				if (!foundLocation && zoneArgs.strictZone)
				{
					logSmartBuildPlacement(message, "black_market", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &zoneArgs.zoneCenter, 0.0f, "zone_strict");
					return moveWorkerToPosition(worker, &zoneArgs.zoneCenter, reason);
				}
			}
			if (!foundLocation)
			{
				foundLocation = findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle);
			}
			if (!foundLocation)
			{
				const Coord3D* moveTarget = zoneArgs.hasZoneCenter
					? &zoneArgs.zoneCenter
					: (anchor != nullptr ? anchor->getPosition() : worker->getPosition());
				logSmartBuildPlacement(message, "black_market", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, moveTarget, 0.0f, zoneArgs.hasZoneCenter ? "zone_fallback" : "anchor_fallback");
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

			logSmartBuildPlacement(message, "black_market", "construct", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &location, angle, zoneArgs.hasZoneCenter ? "zone_or_anchor" : "anchor");
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildScudStormSmartSingle(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr || TheGameLogic == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* worker = resolveWorkerFromArgs(player, message, true, reason);
			if (worker == nullptr)
			{
				return false;
			}

			std::string buildingTemplateName;
			Int requestedAnchorId = -1;
			ZonePlacementArgs zoneArgs = {};
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				zoneArgs = parseZonePlacementArgs(&(*argsIt));
				buildingTemplateName = getJsonString(*argsIt, "building_template");
				const auto anchorIdIt = argsIt->find("anchor_object_id");
				if (anchorIdIt != argsIt->end() && anchorIdIt->is_number_integer())
				{
					requestedAnchorId = anchorIdIt->get<Int>();
				}
			}
			if (buildingTemplateName.empty())
			{
				buildingTemplateName = inferScudStormTemplateForPlayer(player, worker);
			}
			if (buildingTemplateName.empty())
			{
				reason = "scud_storm_prereq_missing";
				return false;
			}

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			Object* anchor = nullptr;
			if (requestedAnchorId > 0)
			{
				anchor = TheGameLogic->findObjectByID(static_cast<ObjectID>(requestedAnchorId));
				if (anchor == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				if (anchor->getControllingPlayer() != player)
				{
					reason = "anchor_not_owned";
					return false;
				}
			}
			if (anchor == nullptr)
			{
				anchor = findPrimaryCommandCenter(player);
			}

			Coord3D location;
			Real angle = 0.0f;
			bool foundLocation = false;
			if (zoneArgs.hasZoneCenter)
			{
				foundLocation = findBuildLocationInZone(player, worker, buildingTemplate, zoneArgs.zoneCenter, zoneArgs.zoneRadius, location, angle);
				if (!foundLocation && zoneArgs.strictZone)
				{
					logSmartBuildPlacement(message, "scud_storm", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &zoneArgs.zoneCenter, 0.0f, "zone_strict");
					return moveWorkerToPosition(worker, &zoneArgs.zoneCenter, reason);
				}
			}
			if (!foundLocation)
			{
				foundLocation = findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle);
			}
			if (!foundLocation)
			{
				const Coord3D* moveTarget = zoneArgs.hasZoneCenter
					? &zoneArgs.zoneCenter
					: (anchor != nullptr ? anchor->getPosition() : worker->getPosition());
				logSmartBuildPlacement(message, "scud_storm", "move", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, moveTarget, 0.0f, zoneArgs.hasZoneCenter ? "zone_fallback" : "anchor_fallback");
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

			logSmartBuildPlacement(message, "scud_storm", "construct", player, worker, buildingTemplateName, requestedAnchorId, anchor, zoneArgs, &location, angle, zoneArgs.hasZoneCenter ? "zone_or_anchor" : "anchor");
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildSupplyStashSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildSupplyStashSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildBarracksSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildBarracksSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildCommandCenterSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildCommandCenterSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildArmsDealerSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildArmsDealerSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildPalaceSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildPalaceSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildBlackMarketSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildBlackMarketSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildScudStormSmart(const nlohmann::json& message, std::string& reason)
		{
			return executeRepeatedBuildAttempts(message, reason, [&](const nlohmann::json& singleMessage, std::string& singleReason) -> bool
			{
				return executeGameBuildScudStormSmartSingle(singleMessage, singleReason);
			});
		}

		bool executeGameBuildBuildingMix(const nlohmann::json& message, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto buildingsIt = argsIt->find("buildings");
			if (buildingsIt == argsIt->end() || !buildingsIt->is_array() || buildingsIt->empty())
			{
				reason = "missing_buildings";
				return false;
			}

			nlohmann::json sharedArgs = *argsIt;
			sharedArgs.erase("buildings");

			auto executeNamedBuild = [&](const std::string& buildCmd, const nlohmann::json& request, std::string& outReason) -> bool
			{
				if (buildCmd == "Game.BuildSupplyStashSmart")
				{
					return executeGameBuildSupplyStashSmart(request, outReason);
				}
				if (buildCmd == "Game.BuildBarracksSmart")
				{
					return executeGameBuildBarracksSmart(request, outReason);
				}
				if (buildCmd == "Game.BuildCommandCenterSmart")
				{
					return executeGameBuildCommandCenterSmart(request, outReason);
				}
				if (buildCmd == "Game.BuildArmsDealerSmart")
				{
					return executeGameBuildArmsDealerSmart(request, outReason);
				}
				if (buildCmd == "Game.BuildPalaceSmart")
				{
					return executeGameBuildPalaceSmart(request, outReason);
				}
				if (buildCmd == "Game.BuildBlackMarketSmart")
				{
					return executeGameBuildBlackMarketSmart(request, outReason);
				}
				if (buildCmd == "Game.BuildScudStormSmart")
				{
					return executeGameBuildScudStormSmart(request, outReason);
				}

				outReason = "unsupported_building_mix_command";
				return false;
			};

			Int issued = 0;
			std::string lastReason = "build_failed";
			for (nlohmann::json::const_iterator it = buildingsIt->begin(); it != buildingsIt->end(); ++it)
			{
				if (!it->is_object())
				{
					lastReason = "invalid_building_mix_entry";
					continue;
				}

				nlohmann::json itemArgs = sharedArgs;
				mergeJsonObjectInto(itemArgs, *it);

				std::string buildCmd = getJsonString(*it, "cmd");
				if (buildCmd.empty())
				{
					const std::string kind = normalizeBuildingMixKind(getJsonString(*it, "kind"));
					if (kind == "supply_stash" || kind == "supply" || kind == "stash")
					{
						buildCmd = "Game.BuildSupplyStashSmart";
					}
					else if (kind == "barracks")
					{
						buildCmd = "Game.BuildBarracksSmart";
					}
					else if (kind == "command_center" || kind == "commandcenter")
					{
						buildCmd = "Game.BuildCommandCenterSmart";
					}
					else if (kind == "arms_dealer" || kind == "armsdealer")
					{
						buildCmd = "Game.BuildArmsDealerSmart";
					}
					else if (kind == "palace")
					{
						buildCmd = "Game.BuildPalaceSmart";
					}
					else if (kind == "black_market" || kind == "blackmarket" || kind == "market")
					{
						buildCmd = "Game.BuildBlackMarketSmart";
					}
					else if (kind == "scud_storm" || kind == "scudstorm" || kind == "scud")
					{
						buildCmd = "Game.BuildScudStormSmart";
					}
				}

				if (buildCmd.empty())
				{
					lastReason = "unsupported_building_mix_kind";
					continue;
				}

				const Int count = parseCountArgFromObject(itemArgs);
				itemArgs["count"] = count;
				itemArgs.erase("kind");
				itemArgs.erase("cmd");

				nlohmann::json buildMessage = message;
				buildMessage["args"] = itemArgs;

				std::string buildReason;
				if (executeNamedBuild(buildCmd, buildMessage, buildReason))
				{
					issued += count;
					continue;
				}

				if (!buildReason.empty())
				{
					lastReason = buildReason;
				}
			}

			if (issued <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		Object* resolveScudStormSourceFromArgs(Player* player, const nlohmann::json& message, const SpecialPowerTemplate*& outTemplate, std::string& reason)
		{
			outTemplate = nullptr;
			if (player == nullptr || TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return nullptr;
			}

			Object* source = nullptr;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto sourceIdIt = argsIt->find("source_object_id");
				if (sourceIdIt != argsIt->end() && sourceIdIt->is_number_integer())
				{
					const Int sourceId = sourceIdIt->get<Int>();
					if (sourceId <= 0)
					{
						reason = "invalid_source_object_id";
						return nullptr;
					}
					source = TheGameLogic->findObjectByID(static_cast<ObjectID>(sourceId));
					if (source == nullptr)
					{
						reason = "source_object_not_found";
						return nullptr;
					}
					if (source->getControllingPlayer() != player)
					{
						reason = "source_object_not_owned";
						return nullptr;
					}
				}
			}

			if (source == nullptr)
			{
				source = player->findMostReadyShortcutSpecialPowerOfType(SPECIAL_SCUD_STORM);
				if (source == nullptr)
				{
					reason = "scud_storm_source_not_found";
					return nullptr;
				}
			}

			if (source->isEffectivelyDead())
			{
				reason = "source_object_dead";
				return nullptr;
			}

			SpecialPowerModuleInterface* mod = source->findSpecialPowerModuleInterface(SPECIAL_SCUD_STORM);
			if (mod == nullptr)
			{
				reason = "source_object_not_scud_storm";
				return nullptr;
			}

			outTemplate = mod->getSpecialPowerTemplate();
			if (outTemplate == nullptr)
			{
				reason = "special_power_template_missing";
				return nullptr;
			}
			if (!mod->isReady())
			{
				reason = "scud_storm_not_ready";
				return nullptr;
			}

			return source;
		}

		bool resolveScudStormTargetPlayerPosition(Player* targetPlayer, Coord3D& target, std::string& reason) const
		{
			target.x = 0.0f;
			target.y = 0.0f;
			target.z = 0.0f;
			if (targetPlayer == nullptr)
			{
				reason = "target_player_not_found";
				return false;
			}

			Object* cc = findPrimaryCommandCenter(targetPlayer);
			if (cc != nullptr && cc->getPosition() != nullptr)
			{
				target = *cc->getPosition();
				target.z = 0.0f;
				return true;
			}

			nlohmann::json targetPos = buildPlayerMapPositionSummary(targetPlayer);
			const auto xIt = targetPos.find("x");
			const auto yIt = targetPos.find("y");
			if (xIt == targetPos.end() || yIt == targetPos.end() || !xIt->is_number() || !yIt->is_number())
			{
				reason = "target_position_unknown";
				return false;
			}

			target.x = xIt->get<Real>();
			target.y = yIt->get<Real>();
			target.z = 0.0f;
			return true;
		}
