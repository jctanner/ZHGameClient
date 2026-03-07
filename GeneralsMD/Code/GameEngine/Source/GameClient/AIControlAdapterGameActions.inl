		static bool containsIgnoreCase(const std::string& haystack, const char* needle)
		{
			if (needle == nullptr || *needle == '\0')
			{
				return false;
			}

			std::string h = haystack;
			std::string n = needle;
			std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return h.find(n) != std::string::npos;
		}

		static std::string inferWorkerTemplateForPlayer(const Player* player)
		{
			if (player == nullptr)
			{
				return std::string();
			}

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				return "GLAInfantryWorker";
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				return "ChinaVehicleDozer";
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				return "AmericaVehicleDozer";
			}
			return std::string();
		}

		struct ProducerSearchContext
		{
			Object* found;
			bool requireCommandCenter;
		};

		static void findProducerCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr)
			{
				return;
			}

			ProducerSearchContext* ctx = static_cast<ProducerSearchContext*>(userData);
			if (ctx->found != nullptr)
			{
				return;
			}
			if (obj->isEffectivelyDead())
			{
				return;
			}
			if (ctx->requireCommandCenter && !obj->isKindOf(KINDOF_COMMANDCENTER))
			{
				return;
			}

			ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
			if (production == nullptr)
			{
				return;
			}

			ctx->found = obj;
		}

		Player* resolvePlayerFromArgs(const nlohmann::json& message, std::string& reason)
		{
			if (ThePlayerList == nullptr)
			{
				reason = "player_state_not_ready";
				return nullptr;
			}

			Player* player = ThePlayerList->getLocalPlayer();
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto playerIndexIt = argsIt->find("player_index");
				if (playerIndexIt != argsIt->end() && playerIndexIt->is_number_integer())
				{
					player = getPlayerByIndex(playerIndexIt->get<Int>());
					if (player == nullptr)
					{
						reason = "player_not_found";
						return nullptr;
					}
				}
			}

			if (player == nullptr)
			{
				reason = "local_player_missing";
				return nullptr;
			}
			return player;
		}

		Object* resolveProducerFromArgs(Player* player, const nlohmann::json& message, bool requireCommandCenter, std::string& reason)
		{
			if (player == nullptr)
			{
				reason = "player_not_found";
				return nullptr;
			}
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return nullptr;
			}

			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto producerIdIt = argsIt->find("producer_object_id");
				if (producerIdIt != argsIt->end() && producerIdIt->is_number_integer())
				{
					const Int producerId = producerIdIt->get<Int>();
					if (producerId <= 0)
					{
						reason = "invalid_producer_object_id";
						return nullptr;
					}

					Object* producer = TheGameLogic->findObjectByID(static_cast<ObjectID>(producerId));
					if (producer == nullptr)
					{
						reason = "producer_not_found";
						return nullptr;
					}
					if (producer->getControllingPlayer() != player)
					{
						reason = "producer_not_owned";
						return nullptr;
					}
					if (requireCommandCenter && !producer->isKindOf(KINDOF_COMMANDCENTER))
					{
						reason = "producer_not_command_center";
						return nullptr;
					}
					if (producer->getProductionUpdateInterface() == nullptr)
					{
						reason = "producer_not_factory";
						return nullptr;
					}
					return producer;
				}
			}

			ProducerSearchContext ctx = { nullptr, requireCommandCenter };
			player->iterateObjects(findProducerCallback, &ctx);
			if (ctx.found == nullptr)
			{
				reason = requireCommandCenter ? "command_center_not_found" : "producer_not_found";
				return nullptr;
			}
			return ctx.found;
		}

		struct SupplyProducerSearchContext
		{
			Object* found;
		};

		static void findSupplyProducerCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr)
			{
				return;
			}
			if (obj->isEffectivelyDead())
			{
				return;
			}
			if (obj->getProductionUpdateInterface() == nullptr)
			{
				return;
			}
			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			if (!containsIgnoreCase(name, "supply"))
			{
				return;
			}
			if (!containsIgnoreCase(name, "stash") && !containsIgnoreCase(name, "center"))
			{
				return;
			}

			SupplyProducerSearchContext* ctx = static_cast<SupplyProducerSearchContext*>(userData);
			if (ctx->found == nullptr)
			{
				ctx->found = obj;
			}
		}

		Object* resolveSupplyProducerFromArgs(Player* player, const nlohmann::json& message, std::string& reason)
		{
			if (player == nullptr)
			{
				reason = "player_not_found";
				return nullptr;
			}
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return nullptr;
			}

			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto producerIdIt = argsIt->find("producer_object_id");
				if (producerIdIt != argsIt->end() && producerIdIt->is_number_integer())
				{
					const Int producerId = producerIdIt->get<Int>();
					if (producerId <= 0)
					{
						reason = "invalid_producer_object_id";
						return nullptr;
					}

					Object* producer = TheGameLogic->findObjectByID(static_cast<ObjectID>(producerId));
					if (producer == nullptr)
					{
						reason = "producer_not_found";
						return nullptr;
					}
					if (producer->getControllingPlayer() != player)
					{
						reason = "producer_not_owned";
						return nullptr;
					}
					if (producer->getProductionUpdateInterface() == nullptr)
					{
						reason = "producer_not_factory";
						return nullptr;
					}
					const ThingTemplate* tt = producer->getTemplate();
					if (tt == nullptr)
					{
						reason = "producer_not_supply";
						return nullptr;
					}
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "supply") || (!containsIgnoreCase(name, "stash") && !containsIgnoreCase(name, "center")))
					{
						reason = "producer_not_supply";
						return nullptr;
					}
					return producer;
				}
			}

			SupplyProducerSearchContext ctx = { nullptr };
			player->iterateObjects(findSupplyProducerCallback, &ctx);
			if (ctx.found == nullptr)
			{
				reason = "supply_producer_not_found";
				return nullptr;
			}
			return ctx.found;
		}

		struct SupplyProducerCollectContext
		{
			std::vector<Object*> producers;
		};

		static void collectSupplyProducersCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr)
			{
				return;
			}
			if (obj->isEffectivelyDead())
			{
				return;
			}
			if (obj->getProductionUpdateInterface() == nullptr)
			{
				return;
			}
			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			if (!containsIgnoreCase(name, "supply"))
			{
				return;
			}
			if (!containsIgnoreCase(name, "stash") && !containsIgnoreCase(name, "center"))
			{
				return;
			}

			SupplyProducerCollectContext* ctx = static_cast<SupplyProducerCollectContext*>(userData);
			ctx->producers.push_back(obj);
		}

		#include "AIControlAdapterWorkers.inl"

		struct SupplySourceInfo
		{
			Object* source;
			Int boxesStored;
			Int cashValue;
		};

		bool collectSupplySources(Int minimumCash, std::vector<SupplySourceInfo>& outSources)
		{
			if (TheGameLogic == nullptr || TheNameKeyGenerator == nullptr)
			{
				return false;
			}

			const NameKeyType warehouseKey = TheNameKeyGenerator->nameToKey("SupplyWarehouseDockUpdate");
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj->isEffectivelyDead())
				{
					continue;
				}

				SupplyWarehouseDockUpdate* warehouse = (SupplyWarehouseDockUpdate*)obj->findUpdateModule(warehouseKey);
				if (warehouse == nullptr)
				{
					continue;
				}

				const Int boxes = warehouse->getBoxesStored();
				const Int cash = boxes * static_cast<Int>(TheGlobalData->m_baseValuePerSupplyBox);
				if (cash < minimumCash)
				{
					continue;
				}

				outSources.push_back({ obj, boxes, cash });
			}

			return true;
		}

		static Real distanceSq2D(const Coord3D* a, const Coord3D* b)
		{
			const Real dx = a->x - b->x;
			const Real dy = a->y - b->y;
			return dx * dx + dy * dy;
		}

		static bool isSupplyDropoffTemplateName(const std::string& templateName)
		{
			if (!containsIgnoreCase(templateName, "supply"))
			{
				return false;
			}
			if (containsIgnoreCase(templateName, "stash") ||
				containsIgnoreCase(templateName, "center") ||
				containsIgnoreCase(templateName, "dropzone"))
			{
				return true;
			}
			return false;
		}

		struct NearbySupplyDropoffSearchContext
		{
			const Coord3D* sourcePos;
			Real maxDistSq;
			NameKeyType centerDockKey;
			bool found;
		};

		static void findNearbySupplyDropoffCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}
			if (obj->isKindOf(KINDOF_FS_SUPPLY_CENTER) || obj->isKindOf(KINDOF_FS_SUPPLY_DROPZONE))
			{
				NearbySupplyDropoffSearchContext* ctx = static_cast<NearbySupplyDropoffSearchContext*>(userData);
				const Coord3D* pos = obj->getPosition();
				if (ctx->sourcePos != nullptr && pos != nullptr && distanceSq2D(ctx->sourcePos, pos) <= ctx->maxDistSq)
				{
					ctx->found = true;
				}
				return;
			}
			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			if (!isSupplyDropoffTemplateName(name))
			{
				NearbySupplyDropoffSearchContext* ctx = static_cast<NearbySupplyDropoffSearchContext*>(userData);
				if (ctx->centerDockKey != NAMEKEY_INVALID && obj->findUpdateModule(ctx->centerDockKey) == nullptr)
				{
					return;
				}
			}
			NearbySupplyDropoffSearchContext* ctx = static_cast<NearbySupplyDropoffSearchContext*>(userData);
			const Coord3D* pos = obj->getPosition();
			if (ctx->sourcePos == nullptr || pos == nullptr)
			{
				return;
			}
			if (distanceSq2D(ctx->sourcePos, pos) <= ctx->maxDistSq)
			{
				ctx->found = true;
			}
		}

		static bool hasNearbyOwnedSupplyDropoff(Player* player, Object* supplySource, Real maxDistance)
		{
			if (player == nullptr || supplySource == nullptr)
			{
				return false;
			}
			NameKeyType centerDockKey = NAMEKEY_INVALID;
			if (TheNameKeyGenerator != nullptr)
			{
				centerDockKey = TheNameKeyGenerator->nameToKey("SupplyCenterDockUpdate");
			}
			NearbySupplyDropoffSearchContext ctx = {
				supplySource->getPosition(),
				maxDistance * maxDistance,
				centerDockKey,
				false
			};
			player->iterateObjects(findNearbySupplyDropoffCallback, &ctx);
			return ctx.found;
		}

		struct NearbyBarracksSearchContext
		{
			const Coord3D* targetPos;
			Real maxDistSq;
			bool found;
		};

		struct NearbyStructureClearanceContext
		{
			const Coord3D* targetPos;
			Real candidateRadius;
			Real extraPadding;
			bool tooClose;
		};

		static void findNearbyBarracksCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}
			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			if (!containsIgnoreCase(name, "barracks"))
			{
				return;
			}

			NearbyBarracksSearchContext* ctx = static_cast<NearbyBarracksSearchContext*>(userData);
			const Coord3D* pos = obj->getPosition();
			if (ctx->targetPos == nullptr || pos == nullptr)
			{
				return;
			}
			if (distanceSq2D(ctx->targetPos, pos) <= ctx->maxDistSq)
			{
				ctx->found = true;
			}
		}

		static bool hasNearbyOwnedBarracks(Player* player, const Coord3D* pos, Real maxDistance)
		{
			if (player == nullptr || pos == nullptr)
			{
				return false;
			}
			NearbyBarracksSearchContext ctx = { pos, maxDistance * maxDistance, false };
			player->iterateObjects(findNearbyBarracksCallback, &ctx);
			return ctx.found;
		}

		static void findNearbyStructureClearanceCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}

			NearbyStructureClearanceContext* ctx = static_cast<NearbyStructureClearanceContext*>(userData);
			if (ctx->tooClose || ctx->targetPos == nullptr)
			{
				return;
			}

			const Coord3D* pos = obj->getPosition();
			if (pos == nullptr)
			{
				return;
			}

			const Real existingRadius = obj->getGeometryInfo().getBoundingCircleRadius();
			const Real required = ctx->candidateRadius + existingRadius + ctx->extraPadding;
			if (distanceSq2D(ctx->targetPos, pos) <= required * required)
			{
				ctx->tooClose = true;
			}
		}

		static bool hasOwnedStructureTooClose(Player* player, const Coord3D* pos, const ThingTemplate* templateToPlace, Real extraPadding)
		{
			if (player == nullptr || pos == nullptr || templateToPlace == nullptr)
			{
				return false;
			}

			Real candidateRadius = templateToPlace->getTemplateGeometryInfo().getBoundingCircleRadius();
			if (candidateRadius < 1.0f)
			{
				candidateRadius = 40.0f;
			}

			NearbyStructureClearanceContext ctx = { pos, candidateRadius, extraPadding, false };
			player->iterateObjects(findNearbyStructureClearanceCallback, &ctx);
			return ctx.tooClose;
		}

		Object* chooseClosestSupplySource(
			const std::vector<SupplySourceInfo>& sources,
			const Coord3D* origin,
			Player* player,
			bool preferUnclaimed,
			Int avoidSourceId = -1)
		{
			Object* best = nullptr;
			Real bestDistSq = 0.0f;
			Object* bestUnclaimed = nullptr;
			Real bestUnclaimedDistSq = 0.0f;

			for (const SupplySourceInfo& info : sources)
			{
				if (info.source == nullptr || info.source->getPosition() == nullptr)
				{
					continue;
				}
				if (avoidSourceId > 0 && static_cast<Int>(info.source->getID()) == avoidSourceId)
				{
					continue;
				}
				const Real distSq = distanceSq2D(info.source->getPosition(), origin);
				if (best == nullptr || distSq < bestDistSq)
				{
					best = info.source;
					bestDistSq = distSq;
				}

				if (!preferUnclaimed)
				{
					continue;
				}
				if (hasNearbyOwnedSupplyDropoff(player, info.source, 650.0f))
				{
					continue;
				}
				if (bestUnclaimed == nullptr || distSq < bestUnclaimedDistSq)
				{
					bestUnclaimed = info.source;
					bestUnclaimedDistSq = distSq;
				}
			}

			if (bestUnclaimed != nullptr)
			{
				return bestUnclaimed;
			}
			return best;
		}

		bool findBuildLocationNearSupply(Player* player, Object* worker, Object* supplySource, const ThingTemplate* buildingTemplate, Coord3D& outLocation, Real& outAngle)
		{
			if (player == nullptr || worker == nullptr || supplySource == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				return false;
			}

			const Coord3D* supplyPos = supplySource->getPosition();
			if (supplyPos == nullptr)
			{
				return false;
			}

			const Real placeAngle = buildingTemplate->getPlacementViewAngle();
			const Real baseRadius = supplySource->getGeometryInfo().getBoundingCircleRadius() + 20.0f;
			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const Coord3D* workerPos = worker->getPosition();

			bool found = false;
			Real bestDistSq = 0.0f;
			Coord3D best = *supplyPos;
			best.z = 0.0f;

			for (Real ring = baseRadius; ring <= baseRadius + 450.0f; ring += 20.0f)
			{
				for (Int i = 0; i < 36; ++i)
				{
					const Real theta = static_cast<Real>(i) * (6.28318530717958647692f / 36.0f);
					Coord3D candidate = *supplyPos;
					candidate.x += std::cos(theta) * ring;
					candidate.y += std::sin(theta) * ring;
					candidate.z = 0.0f;

					if (TheBuildAssistant->isLocationLegalToBuild(&candidate, buildingTemplate, placeAngle, legalOpts, worker, nullptr) != LBC_OK)
					{
						continue;
					}

					const Real distSq = workerPos != nullptr ? distanceSq2D(&candidate, workerPos) : 0.0f;
					if (!found || distSq < bestDistSq)
					{
						found = true;
						bestDistSq = distSq;
						best = candidate;
					}
				}
			}

			if (TheTerrainVisual != nullptr)
			{
				TheTerrainVisual->removeAllBibs();
			}

			if (!found)
			{
				return false;
			}

			outLocation = best;
			outAngle = placeAngle;
			return true;
		}

		std::string inferSupplyBuildingTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLASupplyStash"))
				{
					return "GLASupplyStash";
				}
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				if (canBuildTemplate("ChinaSupplyCenter"))
				{
					return "ChinaSupplyCenter";
				}
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				if (canBuildTemplate("AmericaSupplyCenter"))
				{
					return "AmericaSupplyCenter";
				}
			}

			const char* knownCandidates[] = {
				"GLASupplyStash",
				"GLASupplyCenter",
				"ChinaSupplyCenter",
				"AmericaSupplyCenter"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "supply"))
					{
						continue;
					}
					if (!containsIgnoreCase(name, "stash") && !containsIgnoreCase(name, "center"))
					{
						continue;
					}
					if (containsIgnoreCase(name, "dock") || containsIgnoreCase(name, "warehouse"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		std::string inferBarracksTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLABarracks"))
				{
					return "GLABarracks";
				}
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				if (canBuildTemplate("ChinaBarracks"))
				{
					return "ChinaBarracks";
				}
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				if (canBuildTemplate("AmericaBarracks"))
				{
					return "AmericaBarracks";
				}
			}

			const char* knownCandidates[] = {
				"GLABarracks",
				"ChinaBarracks",
				"AmericaBarracks"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "barracks"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		std::string inferCommandCenterTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLACommandCenter"))
				{
					return "GLACommandCenter";
				}
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				if (canBuildTemplate("ChinaCommandCenter"))
				{
					return "ChinaCommandCenter";
				}
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				if (canBuildTemplate("AmericaCommandCenter"))
				{
					return "AmericaCommandCenter";
				}
			}

			const char* knownCandidates[] = {
				"GLACommandCenter",
				"ChinaCommandCenter",
				"AmericaCommandCenter"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "command") || !containsIgnoreCase(name, "center"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		std::string inferArmsDealerTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLAArmsDealer"))
				{
					return "GLAArmsDealer";
				}
			}

			const char* knownCandidates[] = {
				"GLAArmsDealer"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "arms") && !containsIgnoreCase(name, "dealer"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}

			return std::string();
		}

		std::string inferPalaceTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLAPalace"))
				{
					return "GLAPalace";
				}
			}

			const char* knownCandidates[] = {
				"GLAPalace"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "palace"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		std::string inferBlackMarketTemplateForPlayer(const Player* player, Object* worker = nullptr) const
		{
			if (player == nullptr || TheThingFactory == nullptr)
			{
				return std::string();
			}

			auto canBuildTemplate = [&](const std::string& templateName) -> bool
			{
				if (templateName.empty())
				{
					return false;
				}
				if (worker == nullptr || TheBuildAssistant == nullptr)
				{
					return true;
				}
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(templateName.c_str()), false);
				if (tt == nullptr)
				{
					return false;
				}
				return TheBuildAssistant->isPossibleToMakeUnit(worker, tt) == TRUE;
			};

			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (canBuildTemplate("GLABlackMarket"))
				{
					return "GLABlackMarket";
				}
			}

			const char* knownCandidates[] = {
				"GLABlackMarket"
			};
			for (const char* candidate : knownCandidates)
			{
				if (canBuildTemplate(candidate))
				{
					return candidate;
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "black") || !containsIgnoreCase(name, "market"))
					{
						continue;
					}
					if (TheBuildAssistant->isPossibleToMakeUnit(worker, tt) != TRUE)
					{
						continue;
					}
					return name;
				}
			}
			return std::string();
		}

		struct CommandCenterSearchContext
		{
			Object* firstCommandCenter;
		};

		static void findCommandCenterCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_COMMANDCENTER))
			{
				return;
			}

			CommandCenterSearchContext* ctx = static_cast<CommandCenterSearchContext*>(userData);
			if (ctx->firstCommandCenter == nullptr)
			{
				ctx->firstCommandCenter = obj;
			}
		}

		Object* findPrimaryCommandCenter(Player* player) const
		{
			if (player == nullptr)
			{
				return nullptr;
			}
			CommandCenterSearchContext ctx = { nullptr };
			player->iterateObjects(findCommandCenterCallback, &ctx);
			return ctx.firstCommandCenter;
		}

		bool findBuildLocationAroundAnchor(
			Player* player,
			Object* worker,
			Object* anchor,
			const ThingTemplate* buildingTemplate,
			Coord3D& outLocation,
			Real& outAngle)
		{
			if (player == nullptr || worker == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				return false;
			}

			const Coord3D* anchorPos = anchor != nullptr ? anchor->getPosition() : nullptr;
			if (anchorPos == nullptr)
			{
				anchorPos = worker->getPosition();
			}
			if (anchorPos == nullptr)
			{
				return false;
			}

			const Real placeAngle = buildingTemplate->getPlacementViewAngle();
			const Real anchorRadius = anchor != nullptr ? anchor->getGeometryInfo().getBoundingCircleRadius() : 80.0f;
			const Real baseRadius = anchorRadius + 90.0f;
			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const Coord3D* workerPos = worker->getPosition();

			bool found = false;
			Real bestDistSq = 0.0f;
			Coord3D best = *anchorPos;
			best.z = 0.0f;
			const bool spacingBarracks = containsIgnoreCase(buildingTemplate->getName().str(), "barracks");
			const Real barracksSpacingRadius = 280.0f;
			const Real structurePadding = 36.0f;

			for (Int pass = 0; pass < 2 && !found; ++pass)
			{
				const bool enforceSpacing = spacingBarracks && pass == 0;
				for (Real ring = baseRadius; ring <= baseRadius + 600.0f; ring += 24.0f)
				{
					for (Int i = 0; i < 48; ++i)
					{
						const Real theta = static_cast<Real>(i) * (6.28318530717958647692f / 48.0f);
						Coord3D candidate = *anchorPos;
						candidate.x += std::cos(theta) * ring;
						candidate.y += std::sin(theta) * ring;
						candidate.z = 0.0f;

						if (TheBuildAssistant->isLocationLegalToBuild(&candidate, buildingTemplate, placeAngle, legalOpts, worker, nullptr) != LBC_OK)
						{
							continue;
						}
						if (enforceSpacing && hasNearbyOwnedBarracks(player, &candidate, barracksSpacingRadius))
						{
							continue;
						}
						if (hasOwnedStructureTooClose(player, &candidate, buildingTemplate, structurePadding))
						{
							continue;
						}

						const Real distSq = workerPos != nullptr ? distanceSq2D(&candidate, workerPos) : 0.0f;
						if (!found || distSq < bestDistSq)
						{
							found = true;
							bestDistSq = distSq;
							best = candidate;
						}
					}
				}
			}

			if (TheTerrainVisual != nullptr)
			{
				TheTerrainVisual->removeAllBibs();
			}

			if (!found)
			{
				return false;
			}

			outLocation = best;
			outAngle = placeAngle;
			return true;
		}

		bool executeConstructAtLocation(Object* worker, const ThingTemplate* buildingTemplate, const Coord3D& location, Real angle, std::string& reason)
		{
			if (worker == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			Player* player = worker->getControllingPlayer();
			if (player == nullptr)
			{
				reason = "player_not_found";
				return false;
			}

			const Money* wallet = player->getMoney();
			const UnsignedInt currentMoney = wallet != nullptr ? wallet->countMoney() : 0u;
			if (buildingTemplate->calcCostToBuild(player) > currentMoney)
			{
				reason = "no_money";
				return false;
			}

			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const LegalBuildCode preLegal = TheBuildAssistant->isLocationLegalToBuild(&location, buildingTemplate, angle, legalOpts, worker, nullptr);
			if (preLegal != LBC_OK)
			{
				switch (preLegal)
				{
				case LBC_SHROUD:
					reason = "blocked_by_shroud";
					break;
				case LBC_OBJECTS_IN_THE_WAY:
					reason = "blocked_by_objects";
					break;
				case LBC_NO_CLEAR_PATH:
					reason = "no_clear_path";
					break;
				case LBC_TOO_CLOSE_TO_SUPPLIES:
					reason = "too_close_to_supply";
					break;
				case LBC_RESTRICTED_TERRAIN:
				case LBC_NOT_FLAT_ENOUGH:
				default:
					reason = "no_legal_build_location";
					break;
				}
				return false;
			}

			if (TheBuildAssistant->buildObjectNow(worker, buildingTemplate, &location, angle, player) == nullptr)
			{
				const CanMakeType canMake = TheBuildAssistant->canMakeUnit(worker, buildingTemplate);
				switch (canMake)
				{
				case CANMAKE_NO_PREREQ:
					reason = "no_prereq";
					return false;
				case CANMAKE_NO_MONEY:
					reason = "no_money";
					return false;
				case CANMAKE_FACTORY_IS_DISABLED:
					reason = "factory_disabled";
					return false;
				default:
					break;
				}

				const LegalBuildCode postLegal = TheBuildAssistant->isLocationLegalToBuild(&location, buildingTemplate, angle, legalOpts, worker, nullptr);
				switch (postLegal)
				{
				case LBC_SHROUD:
					reason = "blocked_by_shroud";
					return false;
				case LBC_OBJECTS_IN_THE_WAY:
					reason = "blocked_by_objects";
					return false;
				case LBC_NO_CLEAR_PATH:
					reason = "no_clear_path";
					return false;
				case LBC_TOO_CLOSE_TO_SUPPLIES:
					reason = "too_close_to_supply";
					return false;
				default:
					break;
				}

				reason = "construct_failed";
				return false;
			}

			// Keep a worker out of selection for a while after issuing construction.
			// This avoids repeatedly interrupting the same builder if AI idle/busy flags
			// lag for a few frames or temporarily report idle.
			reserveWorkerForBuild(worker, 45000u);
			return true;
		}

		bool executeGameQueueUnit(const nlohmann::json& message, std::string& reason)
		{
			if (TheThingFactory == nullptr)
			{
				reason = "thing_factory_not_ready";
				return false;
			}
			if (TheBuildAssistant == nullptr)
			{
				reason = "build_assistant_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string unitTemplateName = getJsonString(*argsIt, "unit_template");
			if (unitTemplateName.empty())
			{
				reason = "missing_unit_template";
				return false;
			}

			const std::string producerKind = getJsonString(*argsIt, "producer_kind");
			const bool requireCommandCenter = producerKind.empty() || producerKind == "command_center";

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* producer = resolveProducerFromArgs(player, message, requireCommandCenter, reason);
			if (producer == nullptr)
			{
				return false;
			}

			const ThingTemplate* unitTemplate = TheThingFactory->findTemplate(AsciiString(unitTemplateName.c_str()), false);
			if (unitTemplate == nullptr)
			{
				reason = "unit_template_not_found";
				return false;
			}

			ProductionUpdateInterface* production = producer->getProductionUpdateInterface();
			if (production == nullptr)
			{
				reason = "producer_not_factory";
				return false;
			}

			const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, unitTemplate);
			if (canMake != CANMAKE_OK)
			{
				switch (canMake)
				{
				case CANMAKE_NO_PREREQ:
					reason = "no_prereq";
					break;
				case CANMAKE_NO_MONEY:
					reason = "no_money";
					break;
				case CANMAKE_FACTORY_IS_DISABLED:
					reason = "factory_disabled";
					break;
				case CANMAKE_QUEUE_FULL:
					reason = "queue_full";
					break;
				case CANMAKE_PARKING_PLACES_FULL:
					reason = "parking_full";
					break;
				case CANMAKE_MAXED_OUT_FOR_PLAYER:
					reason = "maxed_out_for_player";
					break;
				default:
					reason = "cannot_make_unit";
					break;
				}
				return false;
			}

			const ProductionID productionId = production->requestUniqueUnitID();
			if (!production->queueCreateUnit(unitTemplate, productionId))
			{
				reason = "unit_queue_rejected_internal";
				return false;
			}

			return true;
		}

		struct ProducerCollectContext
		{
			std::vector<Object*> producers;
			bool matchBarracks;
			bool matchWarFactory;
		};

		static void collectProducersCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}
			if (obj->getProductionUpdateInterface() == nullptr)
			{
				return;
			}

			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const std::string name = tt->getName().str();
			ProducerCollectContext* ctx = static_cast<ProducerCollectContext*>(userData);

			if (ctx->matchBarracks && containsIgnoreCase(name, "barracks"))
			{
				ctx->producers.push_back(obj);
				return;
			}
			if (ctx->matchWarFactory && (containsIgnoreCase(name, "warfactory") || containsIgnoreCase(name, "armsdealer")))
			{
				ctx->producers.push_back(obj);
				return;
			}
		}

		static Int parseQueueCountArg(const nlohmann::json& message)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				return 1;
			}
			const auto countIt = argsIt->find("count");
			if (countIt == argsIt->end() || !countIt->is_number_integer())
			{
				return 1;
			}
			Int value = countIt->get<Int>();
			if (value < 1)
			{
				value = 1;
			}
			if (value > 9)
			{
				value = 9;
			}
			return value;
		}

		std::string inferSoldierTemplateForPlayer(const Player* player, Object* producer) const
		{
			if (player == nullptr || producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
			{
				return std::string();
			}

			std::vector<std::string> candidates;
			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				candidates.push_back("GLAInfantryRebel");
			}
			if (containsIgnoreCase(side, "china") || containsIgnoreCase(baseSide, "china"))
			{
				candidates.push_back("ChinaInfantryRedguard");
			}
			if (containsIgnoreCase(side, "america") || containsIgnoreCase(baseSide, "america") || containsIgnoreCase(side, "usa") || containsIgnoreCase(baseSide, "usa"))
			{
				candidates.push_back("AmericaInfantryRanger");
			}
			candidates.push_back("GLAInfantryRebel");
			candidates.push_back("ChinaInfantryRedguard");
			candidates.push_back("AmericaInfantryRanger");

			for (const std::string& name : candidates)
			{
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name.c_str()), false);
				if (tt == nullptr)
				{
					continue;
				}
				if (TheBuildAssistant->canMakeUnit(producer, tt) == CANMAKE_OK)
				{
					return name;
				}
			}
			return std::string();
		}

		std::string inferQuadTemplateForProducer(Object* producer) const
		{
			if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
			{
				return std::string();
			}
			auto isPotentiallyQueueable = [&](const ThingTemplate* tt) -> bool
			{
				if (tt == nullptr)
				{
					return false;
				}
				const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, tt);
				return canMake == CANMAKE_OK ||
					canMake == CANMAKE_NO_MONEY ||
					canMake == CANMAKE_QUEUE_FULL ||
					canMake == CANMAKE_PARKING_PLACES_FULL;
			};
			const char* candidates[] = {
				"GLAVehicleQuadCannon",
				"GLAVehicleQuadcannon",
				"GLAQuadCannon",
				"GLAVehicleQuad",
				"GLAQuad"
			};
			for (const char* name : candidates)
			{
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
				if (tt == nullptr)
				{
					continue;
				}
				if (isPotentiallyQueueable(tt))
				{
					return name;
				}
			}

			// Fallback: scan all templates for likely quad vehicle names.
			for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
			{
				const std::string templateName = tt->getName().str();
				if (!containsIgnoreCase(templateName, "quad"))
				{
					continue;
				}
				if (!containsIgnoreCase(templateName, "vehicle") && !containsIgnoreCase(templateName, "cannon"))
				{
					continue;
				}
				if (!isPotentiallyQueueable(tt))
				{
					continue;
				}
				return templateName;
			}
			return std::string();
		}

		std::string inferRpgTemplateForProducer(Object* producer) const
		{
			if (producer == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
			{
				return std::string();
			}
			auto isPotentiallyQueueable = [&](const ThingTemplate* tt) -> bool
			{
				if (tt == nullptr)
				{
					return false;
				}
				const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, tt);
				// Accept queueable-now and queueable-in-principle states.
				return canMake == CANMAKE_OK ||
					canMake == CANMAKE_NO_MONEY ||
					canMake == CANMAKE_QUEUE_FULL ||
					canMake == CANMAKE_PARKING_PLACES_FULL;
			};
			const char* candidates[] = {
				"GLAInfantryTunnelDefender",
				"GLAInfantryRPGTrooper",
				"GLAInfantryRPGRocket",
				"GLAInfantryRPG"
			};
			for (const char* name : candidates)
			{
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
				if (tt == nullptr)
				{
					continue;
				}
				if (isPotentiallyQueueable(tt))
				{
					return name;
				}
			}

			// Fallback: scan all templates for likely RPG infantry names.
			for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
			{
				const std::string templateName = tt->getName().str();
				if (!containsIgnoreCase(templateName, "rpg"))
				{
					continue;
				}
				if (!containsIgnoreCase(templateName, "infantry") && !containsIgnoreCase(templateName, "trooper"))
				{
					continue;
				}
				if (!isPotentiallyQueueable(tt))
				{
					continue;
				}
				return templateName;
			}
			return std::string();
		}

		std::string inferScorpionTemplateForProducer(Object* producer) const
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
				if (tt == nullptr)
				{
					continue;
				}
				if (TheBuildAssistant->canMakeUnit(producer, tt) == CANMAKE_OK)
				{
					return name;
				}
			}
			return std::string();
		}

		bool queueTemplateAtProducer(const nlohmann::json& message, Object* producer, const std::string& unitTemplateName, std::string& reason)
		{
			if (producer == nullptr || unitTemplateName.empty())
			{
				reason = "invalid_queue_target";
				return false;
			}

			nlohmann::json queuedMessage = message;
			nlohmann::json queueArgs = nlohmann::json::object();
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				queueArgs = *argsIt;
			}
			queueArgs["producer_kind"] = "any";
			queueArgs["producer_object_id"] = static_cast<Int>(producer->getID());
			queueArgs["unit_template"] = unitTemplateName;
			queuedMessage["args"] = queueArgs;

			return executeGameQueueUnit(queuedMessage, reason);
		}

		bool executeGameQueueSoldiersAllBarracks(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			ProducerCollectContext ctx;
			ctx.matchBarracks = true;
			ctx.matchWarFactory = false;
			player->iterateObjects(collectProducersCallback, &ctx);
			if (ctx.producers.empty())
			{
				reason = "no_barracks_found";
				return false;
			}

			const Int count = parseQueueCountArg(message);
			Int queued = 0;
			std::string lastReason = "queue_failed";
			for (Object* producer : ctx.producers)
			{
				const std::string unitTemplateName = inferSoldierTemplateForPlayer(player, producer);
				if (unitTemplateName.empty())
				{
					continue;
				}
				for (Int i = 0; i < count; ++i)
				{
					std::string queueReason;
					if (queueTemplateAtProducer(message, producer, unitTemplateName, queueReason))
					{
						++queued;
						continue;
					}
					lastReason = queueReason;
					if (queueReason == "queue_full" || queueReason == "no_money")
					{
						break;
					}
				}
			}

			if (queued <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		bool executeGameQueueRpgTroopersAllBarracks(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			ProducerCollectContext ctx;
			ctx.matchBarracks = true;
			ctx.matchWarFactory = false;
			player->iterateObjects(collectProducersCallback, &ctx);
			if (ctx.producers.empty())
			{
				reason = "no_barracks_found";
				return false;
			}

			const Int count = parseQueueCountArg(message);
			Int queued = 0;
			std::string lastReason = "queue_failed";
			for (Object* producer : ctx.producers)
			{
				const std::string unitTemplateName = inferRpgTemplateForProducer(producer);
				if (unitTemplateName.empty())
				{
					lastReason = "rpg_template_not_found";
					continue;
				}
				for (Int i = 0; i < count; ++i)
				{
					std::string queueReason;
					if (queueTemplateAtProducer(message, producer, unitTemplateName, queueReason))
					{
						++queued;
						continue;
					}
					lastReason = queueReason;
					if (queueReason == "queue_full" || queueReason == "no_money")
					{
						break;
					}
				}
			}

			if (queued <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		bool executeGameQueueQuadsAllWarFactories(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			ProducerCollectContext ctx;
			ctx.matchBarracks = false;
			ctx.matchWarFactory = true;
			player->iterateObjects(collectProducersCallback, &ctx);
			if (ctx.producers.empty())
			{
				reason = "no_war_factory_found";
				return false;
			}

			const Int count = parseQueueCountArg(message);
			Int queued = 0;
			std::string lastReason = "queue_failed";
			for (Object* producer : ctx.producers)
			{
				const std::string unitTemplateName = inferQuadTemplateForProducer(producer);
				if (unitTemplateName.empty())
				{
					lastReason = "quad_template_not_found";
					continue;
				}
				for (Int i = 0; i < count; ++i)
				{
					std::string queueReason;
					if (queueTemplateAtProducer(message, producer, unitTemplateName, queueReason))
					{
						++queued;
						continue;
					}
					lastReason = queueReason;
					if (queueReason == "queue_full" || queueReason == "no_money")
					{
						break;
					}
				}
			}

			if (queued <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		bool executeGameQueueScorpionsAllWarFactories(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			ProducerCollectContext ctx;
			ctx.matchBarracks = false;
			ctx.matchWarFactory = true;
			player->iterateObjects(collectProducersCallback, &ctx);
			if (ctx.producers.empty())
			{
				reason = "no_war_factory_found";
				return false;
			}

			const Int count = parseQueueCountArg(message);
			Int queued = 0;
			std::string lastReason = "queue_failed";
			for (Object* producer : ctx.producers)
			{
				const std::string unitTemplateName = inferScorpionTemplateForProducer(producer);
				if (unitTemplateName.empty())
				{
					continue;
				}
				for (Int i = 0; i < count; ++i)
				{
					std::string queueReason;
					if (queueTemplateAtProducer(message, producer, unitTemplateName, queueReason))
					{
						++queued;
						continue;
					}
					lastReason = queueReason;
					if (queueReason == "queue_full" || queueReason == "no_money")
					{
						break;
					}
				}
			}

			if (queued <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		bool executeGameBuildWorker(const nlohmann::json& message, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			bool requireCommandCenter = true;
			bool requireSupplyProducer = false;
			bool explicitProducerId = false;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto producerIdIt = argsIt->find("producer_object_id");
				if (producerIdIt != argsIt->end() && producerIdIt->is_number_integer())
				{
					explicitProducerId = true;
				}
				const std::string producerKind = getJsonString(*argsIt, "producer_kind");
				if (!producerKind.empty() && producerKind == "any")
				{
					requireCommandCenter = false;
				}
				else if (
					producerKind == "supply_stash" ||
					producerKind == "supply_center" ||
					producerKind == "supply")
				{
					requireCommandCenter = false;
					requireSupplyProducer = true;
				}
			}

			std::vector<Object*> targetProducers;
			if (requireSupplyProducer && !explicitProducerId)
			{
				SupplyProducerCollectContext ctx;
				player->iterateObjects(collectSupplyProducersCallback, &ctx);
				if (ctx.producers.empty())
				{
					reason = "supply_producer_not_found";
					return false;
				}
				targetProducers = ctx.producers;
			}
			else
			{
				Object* producer = nullptr;
				if (requireSupplyProducer)
				{
					producer = resolveSupplyProducerFromArgs(player, message, reason);
				}
				else
				{
					producer = resolveProducerFromArgs(player, message, requireCommandCenter, reason);
				}
				if (producer == nullptr)
				{
					return false;
				}
				targetProducers.push_back(producer);
			}

			std::string unitTemplateName;
			if (argsIt != message.end() && argsIt->is_object())
			{
				unitTemplateName = getJsonString(*argsIt, "unit_template");
			}

			// Try best-guess template first.
			std::vector<std::string> candidates;
			const std::string inferred = inferWorkerTemplateForPlayer(player);
			if (!inferred.empty())
			{
				candidates.push_back(inferred);
			}
			candidates.push_back("GLAInfantryWorker");
			candidates.push_back("AmericaVehicleDozer");
			candidates.push_back("ChinaVehicleDozer");

			auto queueWorkerAtProducer = [&](Object* targetProducer, std::string& outReason) -> bool
			{
				if (targetProducer == nullptr)
				{
					outReason = "producer_not_found";
					return false;
				}

				// Explicit template requested: use it directly for this producer.
				if (!unitTemplateName.empty())
				{
					nlohmann::json queuedMessage = message;
					nlohmann::json queueArgs = nlohmann::json::object();
					if (argsIt != message.end() && argsIt->is_object())
					{
						queueArgs = *argsIt;
					}
					queueArgs["unit_template"] = unitTemplateName;
					queueArgs["producer_kind"] = "any";
					queueArgs["producer_object_id"] = static_cast<Int>(targetProducer->getID());
					queuedMessage["args"] = queueArgs;
					return executeGameQueueUnit(queuedMessage, outReason);
				}

				auto queueCandidate = [&](const std::string& candidateTemplate) -> bool
				{
					nlohmann::json queueArgs = nlohmann::json::object();
					if (argsIt != message.end() && argsIt->is_object())
					{
						queueArgs = *argsIt;
					}
					queueArgs["unit_template"] = candidateTemplate;
					queueArgs["producer_kind"] = "any";
					queueArgs["producer_object_id"] = static_cast<Int>(targetProducer->getID());
					nlohmann::json queuedMessage = message;
					queuedMessage["args"] = queueArgs;
					std::string candidateReason;
					if (executeGameQueueUnit(queuedMessage, candidateReason))
					{
						return true;
					}
					outReason = candidateReason;
					return false;
				};

				for (const std::string& candidate : candidates)
				{
					if (candidate.empty())
					{
						continue;
					}
					if (queueCandidate(candidate))
					{
						return true;
					}
				}

				// Fallback scan: pick first Worker/Dozer template this producer can make.
				if (TheThingFactory != nullptr && TheBuildAssistant != nullptr)
				{
					for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
					{
						const std::string templateName = tt->getName().str();
						if (!containsIgnoreCase(templateName, "worker") && !containsIgnoreCase(templateName, "dozer"))
						{
							continue;
						}
						if (TheBuildAssistant->canMakeUnit(targetProducer, tt) != CANMAKE_OK)
						{
							continue;
						}
						if (queueCandidate(templateName))
						{
							return true;
						}
					}
				}
				return false;
			};

			Int queuedCount = 0;
			std::string lastReason = "queue_failed";
			for (Object* targetProducer : targetProducers)
			{
				std::string producerReason;
				if (queueWorkerAtProducer(targetProducer, producerReason))
				{
					++queuedCount;
				}
				else if (!producerReason.empty())
				{
					lastReason = producerReason;
				}
			}

			if (queuedCount <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		bool executeGameFindSupplySources(const nlohmann::json& message, nlohmann::json& result, std::string& reason)
		{
			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Int minimumCash = 1;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto minCashIt = argsIt->find("minimum_cash");
				if (minCashIt != argsIt->end() && minCashIt->is_number_integer())
				{
					minimumCash = minCashIt->get<Int>();
				}
			}

			std::vector<SupplySourceInfo> sources;
			if (!collectSupplySources(minimumCash, sources))
			{
				reason = "supply_scan_not_ready";
				return false;
			}

			nlohmann::json rows = nlohmann::json::array();
			for (const SupplySourceInfo& info : sources)
			{
				const Coord3D* pos = info.source->getPosition();
				rows.push_back(nlohmann::json{
					{"object_id", static_cast<Int>(info.source->getID())},
					{"owner_player_index", info.source->getControllingPlayer() != nullptr ? info.source->getControllingPlayer()->getPlayerIndex() : -1},
					{"template_name", info.source->getTemplate() != nullptr ? info.source->getTemplate()->getName().str() : ""},
					{"boxes_stored", info.boxesStored},
					{"cash_value", info.cashValue},
					{"x", pos != nullptr ? pos->x : 0.0f},
					{"y", pos != nullptr ? pos->y : 0.0f},
					{"z", pos != nullptr ? pos->z : 0.0f}
				});
			}

			result = nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"minimum_cash", minimumCash},
				{"count", rows.size()},
				{"sources", rows}
			};
			return true;
		}

		bool executeGameFindBuildLocationNearSupply(const nlohmann::json& message, nlohmann::json& result, std::string& reason)
		{
			if (TheThingFactory == nullptr)
			{
				reason = "thing_factory_not_ready";
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
			Int minimumCash = 1;
			Int requestedSupplyId = -1;
			Int avoidSupplyId = -1;
			bool rememberSelectedSupply = false;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const std::string requestedTemplate = getJsonString(*argsIt, "building_template");
				if (!requestedTemplate.empty())
				{
					buildingTemplateName = requestedTemplate;
				}
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
				const auto avoidIdIt = argsIt->find("avoid_supply_source_id");
				if (avoidIdIt != argsIt->end() && avoidIdIt->is_number_integer())
				{
					avoidSupplyId = avoidIdIt->get<Int>();
				}
				const auto rememberIt = argsIt->find("remember_supply_choice");
				if (rememberIt != argsIt->end() && rememberIt->is_boolean())
				{
					rememberSelectedSupply = rememberIt->get<bool>();
				}
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

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(buildingTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
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
				selectedSupply = chooseClosestSupplySource(sources, worker->getPosition(), player, true, avoidSupplyId);
				if (selectedSupply == nullptr)
				{
					reason = "supply_source_not_found";
					return false;
				}
			}

			Coord3D location;
			Real angle = 0.0f;
			if (!findBuildLocationNearSupply(player, worker, selectedSupply, buildingTemplate, location, angle))
			{
				reason = "no_legal_build_location";
				return false;
			}

			result = nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"worker_object_id", static_cast<Int>(worker->getID())},
				{"supply_source_id", static_cast<Int>(selectedSupply->getID())},
				{"building_template", buildingTemplateName},
				{"angle", angle},
				{"location", nlohmann::json{{"x", location.x}, {"y", location.y}, {"z", location.z}}}
			};
			if (rememberSelectedSupply)
			{
				m_lastAutoSupplySourceByPlayer[player->getPlayerIndex()] = static_cast<Int>(selectedSupply->getID());
			}
			return true;
		}

		bool executeGameDozerConstruct(const nlohmann::json& message, std::string& reason)
		{
			nlohmann::json buildResult;
			if (!executeGameFindBuildLocationNearSupply(message, buildResult, reason))
			{
				return false;
			}

			if (TheGameLogic == nullptr || TheThingFactory == nullptr || TheBuildAssistant == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			const std::string buildingTemplateName = argsIt != message.end() && argsIt->is_object()
				? getJsonString(*argsIt, "building_template")
				: std::string();
			const std::string finalTemplateName = !buildingTemplateName.empty()
				? buildingTemplateName
				: buildResult.value("building_template", std::string());

			const ThingTemplate* buildingTemplate = TheThingFactory->findTemplate(AsciiString(finalTemplateName.c_str()), false);
			if (buildingTemplate == nullptr)
			{
				reason = "building_template_not_found";
				return false;
			}

			const Int workerId = buildResult.value("worker_object_id", 0);
			if (workerId <= 0)
			{
				reason = "worker_not_found";
				return false;
			}
			Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(workerId));
			if (worker == nullptr)
			{
				reason = "worker_not_found";
				return false;
			}

			const auto loc = buildResult["location"];
			Coord3D location;
			location.x = loc.value("x", 0.0f);
			location.y = loc.value("y", 0.0f);
			location.z = 0.0f;
			const Real angle = buildResult.value("angle", buildingTemplate->getPlacementViewAngle());

			Player* player = worker->getControllingPlayer();
			if (player == nullptr)
			{
				reason = "player_not_found";
				return false;
			}
			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

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

		bool executeGameBuildSupplyStashSmart(const nlohmann::json& message, std::string& reason)
		{
			// First try the normal auto-build path.
			std::string buildReason;
			if (executeGameBuildSupplyStashAuto(message, buildReason))
			{
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

			std::string buildingTemplateName;
			Int minimumCash = 1;
			Int requestedSupplyId = -1;
			Int avoidSupplyId = -1;
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
				selectedSupply = chooseClosestSupplySource(sources, worker->getPosition(), player, true, avoidSupplyId);
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

			Coord3D revealPos;
			Real revealAngle = 0.0f;
			if (findBuildLocationNearSupply(player, worker, selectedSupply, buildingTemplate, revealPos, revealAngle))
			{
				// Placement should now be legal; retry construct immediately.
				std::string retryReason;
				if (executeGameBuildSupplyStashAuto(message, retryReason))
				{
					return true;
				}
				reason = retryReason;
				return false;
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
			ai->aiMoveToPosition(&target, CMD_FROM_AI);
			return true;
		}

		bool executeGameBuildBarracksSmart(const nlohmann::json& message, std::string& reason)
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
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
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
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor != nullptr ? anchor->getPosition() : worker->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildCommandCenterSmart(const nlohmann::json& message, std::string& reason)
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
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
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
			if (anchor == nullptr)
			{
				reason = "anchor_not_found";
				return false;
			}

			Coord3D location;
			Real angle = 0.0f;
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildArmsDealerSmart(const nlohmann::json& message, std::string& reason)
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
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
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
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor != nullptr ? anchor->getPosition() : worker->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildPalaceSmart(const nlohmann::json& message, std::string& reason)
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
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
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
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor != nullptr ? anchor->getPosition() : worker->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
		}

		bool executeGameBuildBlackMarketSmart(const nlohmann::json& message, std::string& reason)
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
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
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
				reason = "black_market_template_unknown";
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
			if (!findBuildLocationAroundAnchor(player, worker, anchor, buildingTemplate, location, angle))
			{
				AIUpdateInterface* ai = worker->getAI();
				if (ai == nullptr)
				{
					reason = "worker_no_ai";
					return false;
				}

				const Coord3D* moveTarget = anchor != nullptr ? anchor->getPosition() : worker->getPosition();
				if (moveTarget == nullptr)
				{
					reason = "anchor_not_found";
					return false;
				}
				Coord3D target = *moveTarget;
				target.z = 0.0f;
				ai->aiMoveToPosition(&target, CMD_FROM_AI);
				return true;
			}

			return executeConstructAtLocation(worker, buildingTemplate, location, angle, reason);
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

			Int commanded = 0;
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

				AIUpdateInterface* ai = obj->getAI();
				if (ai == nullptr)
				{
					continue;
				}

				ai->aiAttackMoveToPosition(&target, 0, CMD_FROM_AI);
				++commanded;
			}

			if (commanded == 0)
			{
				reason = "no_valid_objects";
				return false;
			}
			return true;
		}

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

			Int commanded = 0;
			for (Object* obj : collectCtx.units)
			{
				AIUpdateInterface* ai = obj->getAI();
				if (ai == nullptr)
				{
					continue;
				}
				ai->aiAttackMoveToPosition(&target, 0, CMD_FROM_AI);
				++commanded;
			}

			if (commanded <= 0)
			{
				reason = "no_valid_objects";
				return false;
			}
			return true;
		}
