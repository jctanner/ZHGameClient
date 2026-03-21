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

		static std::string trimAscii(const std::string& value)
		{
			std::string::size_type start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
			{
				++start;
			}
			std::string::size_type end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
			{
				--end;
			}
			return value.substr(start, end - start);
		}

		static std::string canonicalizeUpgradeName(const std::string& rawName)
		{
			const std::string trimmed = trimAscii(rawName);
			if (trimmed.empty())
			{
				return trimmed;
			}

			std::string key = trimmed;
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			static const std::unordered_map<std::string, std::string> aliases = {
				{"upgrade_glaanthraxgamma", "Chem_Upgrade_GLAAnthraxGamma"},
				{"upgrade_glaquadcannonsnipe", "GC_Slth_Upgrade_GLAQuadCannonSnipe"},
				{"upgrade_glaquadcannonsnipegun", "GC_Slth_Upgrade_GLAQuadCannonSnipe"},
				{"upgrade_glademotraphighexplosivebomb", "Demo_Upgrade_GLADemoTrapHighExplosiveBomb"},
				{"upgrade_glademotraphigh explosivebomb", "Demo_Upgrade_GLADemoTrapHighExplosiveBomb"}
			};

			const std::unordered_map<std::string, std::string>::const_iterator it = aliases.find(key);
			if (it != aliases.end())
			{
				return it->second;
			}
			return trimmed;
		}

		static std::string canonicalizeScienceName(const std::string& rawName)
		{
			const std::string trimmed = trimAscii(rawName);
			if (trimmed.empty())
			{
				return trimmed;
			}

			std::string key = trimmed;
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			static const std::unordered_map<std::string, std::string> aliases = {
				{"science_glacashbounty1", "SCIENCE_CashBounty1"},
				{"science_glacashbounty2", "SCIENCE_CashBounty2"},
				{"science_glacashbounty3", "SCIENCE_CashBounty3"},
				{"science_glarebelambush1", "SCIENCE_RebelAmbush1"},
				{"science_glarebelambush2", "SCIENCE_RebelAmbush2"},
				{"science_glarebelambush3", "SCIENCE_RebelAmbush3"},
				{"science_glasneakattack", "SCIENCE_SneakAttack"},
				{"science_glascudlauncher", "SCIENCE_ScudLauncher"},
				{"science_glamaraudertank", "SCIENCE_MarauderTank"},
				{"science_glatechnicaltraining", "SCIENCE_TechnicalTraining"},
				{"science_glahijacker", "SCIENCE_Hijacker"},
				{"science_glaanthraxbomb", "SCIENCE_AnthraxBomb"},
				{"science_glagpsscrambler", "SCIENCE_GPSScrambler"}
			};

			const std::unordered_map<std::string, std::string>::const_iterator it = aliases.find(key);
			if (it != aliases.end())
			{
				return it->second;
			}
			return trimmed;
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

		static bool matchesUpgradeProducerKind(const ThingTemplate* tt, const std::string& producerKind)
		{
			if (tt == nullptr)
			{
				return false;
			}
			if (producerKind.empty() || producerKind == "any")
			{
				return true;
			}

			const std::string name = tt->getName().str();
			if (producerKind == "palace")
			{
				return containsIgnoreCase(name, "palace");
			}
			if (producerKind == "black_market" || producerKind == "blackmarket" || producerKind == "market")
			{
				return containsIgnoreCase(name, "black") && containsIgnoreCase(name, "market");
			}
			return false;
		}

		struct UpgradeProducerSearchContext
		{
			Object* found;
			std::string producerKind;
		};

		static void findUpgradeProducerCallback(Object* obj, void* userData)
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

			UpgradeProducerSearchContext* ctx = static_cast<UpgradeProducerSearchContext*>(userData);
			if (ctx->found != nullptr)
			{
				return;
			}

			const ThingTemplate* tt = obj->getTemplate();
			if (!matchesUpgradeProducerKind(tt, ctx->producerKind))
			{
				return;
			}

			ctx->found = obj;
		}

		struct UpgradeCapableProducerSearchContext
		{
			Object* found;
			std::string producerKind;
			const UpgradeTemplate* upgrade;
		};

		static void findUpgradeCapableProducerCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}

			UpgradeCapableProducerSearchContext* ctx = static_cast<UpgradeCapableProducerSearchContext*>(userData);
			if (ctx->found != nullptr)
			{
				return;
			}
			if (!matchesUpgradeProducerKind(obj->getTemplate(), ctx->producerKind))
			{
				return;
			}

			ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
			if (production == nullptr)
			{
				return;
			}
			if (ctx->upgrade == nullptr || !obj->canProduceUpgrade(ctx->upgrade))
			{
				return;
			}

			ctx->found = obj;
		}

		static bool messageHasExplicitProducerObjectId(const nlohmann::json& message)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				return false;
			}
			const auto producerIdIt = argsIt->find("producer_object_id");
			return producerIdIt != argsIt->end() && producerIdIt->is_number_integer() && producerIdIt->get<Int>() > 0;
		}

		Object* resolveUpgradeCapableProducer(Player* player, const std::string& producerKind, const UpgradeTemplate* upgradeT)
		{
			if (player == nullptr || upgradeT == nullptr)
			{
				return nullptr;
			}

			UpgradeCapableProducerSearchContext ctx = {};
			ctx.found = nullptr;
			ctx.producerKind = producerKind;
			ctx.upgrade = upgradeT;
			player->iterateObjects(findUpgradeCapableProducerCallback, &ctx);
			return ctx.found;
		}

		Object* resolveUpgradeProducerFromArgs(Player* player, const nlohmann::json& message, std::string& reason)
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
			std::string producerKind;
			if (argsIt != message.end() && argsIt->is_object())
			{
				producerKind = getJsonString(*argsIt, "producer_kind");
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
					if (!matchesUpgradeProducerKind(producer->getTemplate(), producerKind))
					{
						reason = "producer_kind_mismatch";
						return nullptr;
					}
					return producer;
				}
			}

			if (producerKind.empty() || producerKind == "any")
			{
				return resolveProducerFromArgs(player, message, false, reason);
			}

			UpgradeProducerSearchContext ctx = {};
			ctx.found = nullptr;
			ctx.producerKind = producerKind;
			player->iterateObjects(findUpgradeProducerCallback, &ctx);
			if (ctx.found == nullptr)
			{
				if (producerKind == "palace")
				{
					reason = "palace_not_found";
				}
				else if (producerKind == "black_market" || producerKind == "blackmarket" || producerKind == "market")
				{
					reason = "black_market_not_found";
				}
				else
				{
					reason = "producer_not_found";
				}
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

		void pruneExpiredBuildLocationReservations()
		{
			if (m_reservedBuildLocations.empty())
			{
				return;
			}

			const DWORD now = ::GetTickCount();
			for (auto it = m_reservedBuildLocations.begin(); it != m_reservedBuildLocations.end(); )
			{
				if (static_cast<LONG>(it->untilTick - now) <= 0)
				{
					it = m_reservedBuildLocations.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		bool isBuildLocationTemporarilyReserved(Player* player, const Coord3D* candidate, const ThingTemplate* buildingTemplate)
		{
			if (player == nullptr || candidate == nullptr || buildingTemplate == nullptr)
			{
				return false;
			}

			pruneExpiredBuildLocationReservations();
			const Int playerIndex = player->getPlayerIndex();
			const std::string templateName = buildingTemplate->getName().str();
			const Real candidateRadius = std::max<Real>(48.0f, buildingTemplate->getTemplateGeometryInfo().getBoundingCircleRadius() + 18.0f);
			for (const PendingBuildLocationReservation& reserved : m_reservedBuildLocations)
			{
				if (reserved.playerIndex != playerIndex)
				{
					continue;
				}

				const Real dx = reserved.location.x - candidate->x;
				const Real dy = reserved.location.y - candidate->y;
				const Real combinedRadius = std::sqrt(std::max<Real>(0.0f, reserved.radiusSq)) + candidateRadius;
				if ((dx * dx) + (dy * dy) < (combinedRadius * combinedRadius))
				{
					return true;
				}
			}
			return false;
		}

		void reserveBuildLocation(Player* player, const Coord3D& location, const ThingTemplate* buildingTemplate, DWORD durationMs = 15000u)
		{
			if (player == nullptr || buildingTemplate == nullptr)
			{
				return;
			}

			pruneExpiredBuildLocationReservations();
			const Real radius = std::max<Real>(48.0f, buildingTemplate->getTemplateGeometryInfo().getBoundingCircleRadius() + 18.0f);
			m_reservedBuildLocations.push_back(PendingBuildLocationReservation{
				player->getPlayerIndex(),
				buildingTemplate->getName().str(),
				location,
				radius * radius,
				::GetTickCount() + durationMs
			});
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

		std::string makeBuildExpansionKey(Player* player, const ThingTemplate* buildingTemplate) const
		{
			if (player == nullptr || buildingTemplate == nullptr)
			{
				return std::string();
			}

			char playerKey[24];
			sprintf_s(playerKey, "%d:", player->getPlayerIndex());
			return std::string(playerKey) + buildingTemplate->getName().str();
		}

		Real getPreferredBuildExpansionRadius(Player* player, const ThingTemplate* buildingTemplate, Real fallbackRadius) const
		{
			const std::string key = makeBuildExpansionKey(player, buildingTemplate);
			if (key.empty())
			{
				return fallbackRadius;
			}

			const auto it = m_buildExpansionRadiusByKey.find(key);
			if (it == m_buildExpansionRadiusByKey.end())
			{
				return fallbackRadius;
			}
			return std::max(fallbackRadius, it->second);
		}

		void noteBuildExpansionRadius(Player* player, const ThingTemplate* buildingTemplate, Real radius)
		{
			const std::string key = makeBuildExpansionKey(player, buildingTemplate);
			if (key.empty())
			{
				return;
			}
			m_buildExpansionRadiusByKey[key] = std::max<Real>(0.0f, radius);
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
					if (isBuildLocationTemporarilyReserved(player, &candidate, buildingTemplate))
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

		std::string inferScudStormTemplateForPlayer(const Player* player, Object* worker = nullptr) const
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
				const char* glaCandidates[] = {
					"GLAScudStorm",
					"Boss_GLAScudStorm",
					"Chem_GLAScudStorm",
					"Demo_GLAScudStorm",
					"Slth_GLAScudStorm"
				};
				for (const char* candidate : glaCandidates)
				{
					if (canBuildTemplate(candidate))
					{
						return candidate;
					}
				}
			}

			if (worker != nullptr && TheBuildAssistant != nullptr)
			{
				for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
				{
					const std::string name = tt->getName().str();
					if (!containsIgnoreCase(name, "scud") || !containsIgnoreCase(name, "storm"))
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

		struct ZonePlacementArgs
		{
			bool hasZoneCenter;
			Coord3D zoneCenter;
			Real zoneRadius;
			bool strictZone;
		};

		static ZonePlacementArgs parseZonePlacementArgs(const nlohmann::json* args)
		{
			ZonePlacementArgs out = {};
			out.hasZoneCenter = false;
			out.zoneCenter.x = 0.0f;
			out.zoneCenter.y = 0.0f;
			out.zoneCenter.z = 0.0f;
			out.zoneRadius = 320.0f;
			out.strictZone = false;
			if (args == nullptr || !args->is_object())
			{
				return out;
			}

			const auto strictIt = args->find("strict_zone");
			if (strictIt != args->end() && strictIt->is_boolean())
			{
				out.strictZone = strictIt->get<bool>();
			}

			const auto zoneCenterIt = args->find("zone_center");
			if (zoneCenterIt != args->end() && zoneCenterIt->is_object())
			{
				const auto zxIt = zoneCenterIt->find("x");
				const auto zyIt = zoneCenterIt->find("y");
				if (zxIt != zoneCenterIt->end() && zyIt != zoneCenterIt->end() && zxIt->is_number() && zyIt->is_number())
				{
					out.zoneCenter.x = zxIt->get<Real>();
					out.zoneCenter.y = zyIt->get<Real>();
					out.zoneCenter.z = 0.0f;
					out.hasZoneCenter = true;
				}
			}
			if (!out.hasZoneCenter)
			{
				const auto zxIt = args->find("zone_x");
				const auto zyIt = args->find("zone_y");
				if (zxIt != args->end() && zyIt != args->end() && zxIt->is_number() && zyIt->is_number())
				{
					out.zoneCenter.x = zxIt->get<Real>();
					out.zoneCenter.y = zyIt->get<Real>();
					out.zoneCenter.z = 0.0f;
					out.hasZoneCenter = true;
				}
			}

			const auto radiusIt = args->find("zone_radius");
			if (radiusIt != args->end() && radiusIt->is_number())
			{
				const Real parsed = radiusIt->get<Real>();
				if (parsed >= 64.0f)
				{
					out.zoneRadius = parsed;
				}
			}
			return out;
		}

		static bool canIssuePlayerScopedMessage(Player* player, std::string& reason)
		{
			if (player == nullptr)
			{
				reason = "player_not_found";
				return false;
			}
			if (TheMessageStream == nullptr)
			{
				reason = "message_stream_not_ready";
				return false;
			}
			if (ThePlayerList == nullptr)
			{
				reason = "player_state_not_ready";
				return false;
			}

			Player* localPlayer = ThePlayerList->getLocalPlayer();
			if (localPlayer == nullptr)
			{
				reason = "local_player_missing";
				return false;
			}

			if (TheGameLogic != nullptr &&
				TheGameLogic->isInMultiplayerGame() &&
				player->getPlayerIndex() != localPlayer->getPlayerIndex())
			{
				reason = "remote_player_control_not_supported_in_multiplayer";
				return false;
			}

			return true;
		}

		static GameMessage* appendPlayerMessage(Player* player, GameMessage::Type type)
		{
			if (TheMessageStream == nullptr)
			{
				return nullptr;
			}

			GameMessage* msg = TheMessageStream->appendMessage(type);
			if (msg != nullptr && player != nullptr)
			{
				msg->friend_setPlayerIndex(player->getPlayerIndex());
			}
			return msg;
		}

		static std::vector<ObjectID> getCurrentSelectionObjectIds(Player* player)
		{
			std::vector<ObjectID> ids;
			if (player == nullptr || TheAI == nullptr)
			{
				return ids;
			}

#if RETAIL_COMPATIBLE_AIGROUP
			AIGroup* group = TheAI->createGroup();
			if (group == nullptr)
			{
				return ids;
			}
			player->getCurrentSelectionAsAIGroup(group);
			ids = group->getAllIDs();
			TheAI->destroyGroup(group);
#else
			AIGroupPtr group = TheAI->createGroup();
			if (group == nullptr)
			{
				return ids;
			}
			player->getCurrentSelectionAsAIGroup(group.Peek());
			ids = group->getAllIDs();
			group->removeAll();
#endif
			return ids;
		}

		static void appendSelectionMessage(Player* player, const std::vector<ObjectID>& ids, bool noSound)
		{
			if (player == nullptr)
			{
				return;
			}

			if (ids.empty())
			{
				appendPlayerMessage(player, GameMessage::MSG_DESTROY_SELECTED_GROUP);
				return;
			}

			GameMessage* msg = appendPlayerMessage(
				player,
				noSound ? GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND : GameMessage::MSG_CREATE_SELECTED_GROUP);
			if (msg == nullptr)
			{
				return;
			}

			msg->appendBooleanArgument(TRUE);
			for (std::vector<ObjectID>::const_iterator it = ids.begin(); it != ids.end(); ++it)
			{
				msg->appendObjectIDArgument(*it);
			}
		}

		template <typename TCommandBuilder>
		bool executeScopedSelectionCommand(
			Player* player,
			const std::vector<ObjectID>& selectedIds,
			std::string& reason,
			TCommandBuilder&& buildCommand)
		{
			if (!canIssuePlayerScopedMessage(player, reason))
			{
				return false;
			}
			if (selectedIds.empty())
			{
				reason = "no_valid_objects";
				return false;
			}

			const std::vector<ObjectID> priorSelection = getCurrentSelectionObjectIds(player);
			appendSelectionMessage(player, selectedIds, true);
			if (!buildCommand())
			{
				appendSelectionMessage(player, priorSelection, true);
				if (reason.empty())
				{
					reason = "command_build_failed";
				}
				return false;
			}
			appendSelectionMessage(player, priorSelection, true);
			return true;
		}

		bool moveWorkerToPosition(Object* worker, const Coord3D* moveTarget, std::string& reason)
		{
			if (worker == nullptr)
			{
				reason = "worker_not_found";
				return false;
			}
			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr)
			{
				reason = "worker_no_ai";
				return false;
			}
			if (moveTarget == nullptr)
			{
				reason = "move_target_not_found";
				return false;
			}
			Player* player = worker->getControllingPlayer();
			if (player == nullptr)
			{
				reason = "player_not_found";
				return false;
			}

			Coord3D target = *moveTarget;
			target.z = 0.0f;
			const ObjectID workerId = worker->getID();
			return executeScopedSelectionCommand(player, std::vector<ObjectID>(1, workerId), reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_MOVETO);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendLocationArgument(target);
				return true;
			});
		}

		bool findBuildLocationInZone(
			Player* player,
			Object* worker,
			const ThingTemplate* buildingTemplate,
			const Coord3D& zoneCenter,
			Real zoneRadius,
			Coord3D& outLocation,
			Real& outAngle)
		{
			if (player == nullptr || worker == nullptr || buildingTemplate == nullptr || TheBuildAssistant == nullptr)
			{
				return false;
			}

			const Real placeAngle = buildingTemplate->getPlacementViewAngle();
			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const Coord3D* workerPos = worker->getPosition();

			bool found = false;
			Real bestDistSq = 0.0f;
			Coord3D best = zoneCenter;
			best.z = 0.0f;
			const bool spacingBarracks = containsIgnoreCase(buildingTemplate->getName().str(), "barracks");
			const Real barracksSpacingRadius = 280.0f;
			const Real structurePadding = 36.0f;
			const Real scanRadius = zoneRadius < 64.0f ? 64.0f : zoneRadius;

			for (Real ring = 0.0f; ring <= scanRadius; ring += 24.0f)
			{
				const Int slices = ring <= 0.01f ? 1 : 48;
				for (Int i = 0; i < slices; ++i)
				{
					Coord3D candidate = zoneCenter;
					if (ring > 0.01f)
					{
						const Real theta = static_cast<Real>(i) * (6.28318530717958647692f / static_cast<Real>(slices));
						candidate.x += std::cos(theta) * ring;
						candidate.y += std::sin(theta) * ring;
					}
					candidate.z = 0.0f;

					if (TheBuildAssistant->isLocationLegalToBuild(&candidate, buildingTemplate, placeAngle, legalOpts, worker, nullptr) != LBC_OK)
					{
						continue;
					}
					if (isBuildLocationTemporarilyReserved(player, &candidate, buildingTemplate))
					{
						continue;
					}
					if (spacingBarracks && hasNearbyOwnedBarracks(player, &candidate, barracksSpacingRadius))
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
			const Real preferredRadius = getPreferredBuildExpansionRadius(player, buildingTemplate, baseRadius + 240.0f);
			const Real ringStep = 24.0f;
			const Real windowSpan = 720.0f;
			const Real maxExtraRadius = 12000.0f;
			Real usedRadius = preferredRadius;

			for (Int pass = 0; pass < 3 && !found; ++pass)
			{
				const bool enforceSpacing = spacingBarracks && pass == 0;
				const Real structurePadding = pass == 0 ? 36.0f : (pass == 1 ? 12.0f : 0.0f);
				Real searchStart = std::max(baseRadius, preferredRadius - 240.0f);
				Real searchEnd = std::min(baseRadius + maxExtraRadius, searchStart + windowSpan);

				while (!found && searchStart <= baseRadius + maxExtraRadius)
				{
					for (Real ring = searchStart; ring <= searchEnd; ring += ringStep)
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
							if (isBuildLocationTemporarilyReserved(player, &candidate, buildingTemplate))
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
								usedRadius = ring;
							}
						}
					}

					if (found)
					{
						break;
					}

					searchStart = searchEnd + ringStep;
					searchEnd = std::min(baseRadius + maxExtraRadius, searchStart + windowSpan);
				}
			}

			if (TheTerrainVisual != nullptr)
			{
				TheTerrainVisual->removeAllBibs();
			}

			if (!found)
			{
				noteBuildExpansionRadius(player, buildingTemplate, std::max(preferredRadius + windowSpan, baseRadius + 720.0f));
				return false;
			}

			noteBuildExpansionRadius(player, buildingTemplate, std::max(baseRadius, usedRadius - 96.0f));
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
			const UnsignedInt buildCost = static_cast<UnsignedInt>(std::max<Int>(0, buildingTemplate->calcCostToBuild(player)));
			if (buildCost > currentMoney)
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

			const Int templateId = buildingTemplate->getTemplateID();
			const ObjectID workerId = worker->getID();
			if (!executeScopedSelectionCommand(player, std::vector<ObjectID>(1, workerId), reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DOZER_CONSTRUCT);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendIntegerArgument(templateId);
				msg->appendLocationArgument(location);
				msg->appendRealArgument(angle);
				return true;
			}))
			{
				return false;
			}

			// Keep a worker out of selection for a while after issuing construction.
			// This avoids repeatedly interrupting the same builder if AI idle/busy flags
			// lag for a few frames or temporarily report idle.
			reserveWorkerForBuild(worker, 45000u);
			reserveBuildLocation(player, location, buildingTemplate);
			return true;
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

		static Int parseCountArgFromObject(const nlohmann::json& obj, const char* key = "count")
		{
			const auto countIt = obj.find(key);
			if (countIt == obj.end() || !countIt->is_number_integer())
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

		static bool parseUnsignedMoneyArg(const nlohmann::json& message, UnsignedInt& outMoney, std::string& reason)
		{
			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const auto moneyIt = argsIt->find("money");
			if (moneyIt == argsIt->end())
			{
				reason = "missing_money";
				return false;
			}

			unsigned long long rawMoney = 0ull;
			if (moneyIt->is_number_unsigned())
			{
				rawMoney = moneyIt->get<unsigned long long>();
			}
			else if (moneyIt->is_number_integer())
			{
				const long long signedMoney = moneyIt->get<long long>();
				if (signedMoney < 0ll)
				{
					reason = "invalid_money";
					return false;
				}
				rawMoney = static_cast<unsigned long long>(signedMoney);
			}
			else
			{
				reason = "invalid_money";
				return false;
			}

			const unsigned long long kMaxMoney = 0xFFFFFFFFull;
			if (rawMoney > kMaxMoney)
			{
				outMoney = static_cast<UnsignedInt>(kMaxMoney);
				return true;
			}

			outMoney = static_cast<UnsignedInt>(rawMoney);
			return true;
		}

		bool executeGameSetMoney(const nlohmann::json& message, std::string& reason)
		{
			if (TheGameLogic == nullptr)
			{
				reason = "logic_not_ready";
				return false;
			}

			if (TheGameLogic->isInReplayGame())
			{
				reason = "replay_not_supported";
				return false;
			}

			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInSkirmishGame())
			{
				reason = "cash_mutation_not_allowed_in_multiplayer";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Money* wallet = player->getMoney();
			if (wallet == nullptr)
			{
				reason = "money_not_available";
				return false;
			}

			UnsignedInt targetMoney = 0u;
			if (!parseUnsignedMoneyArg(message, targetMoney, reason))
			{
				return false;
			}

			const UnsignedInt currentMoney = wallet->countMoney();
			if (currentMoney > 0u)
			{
				wallet->withdraw(currentMoney, FALSE);
			}
			if (targetMoney > 0u)
			{
				wallet->deposit(targetMoney, FALSE, FALSE);
			}
			return true;
		}

		template <typename TSingleAttempt>
		bool executeRepeatedBuildAttempts(const nlohmann::json& message, std::string& reason, TSingleAttempt&& singleAttempt)
		{
			const Int count = parseQueueCountArg(message);
			if (count <= 1)
			{
				return singleAttempt(message, reason);
			}

			nlohmann::json singleMessage = message;
			nlohmann::json singleArgs = nlohmann::json::object();
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				singleArgs = *argsIt;
			}
			singleArgs["count"] = 1;
			singleMessage["args"] = singleArgs;

			Int issued = 0;
			std::string lastReason = "build_failed";
			for (Int i = 0; i < count; ++i)
			{
				std::string attemptReason;
				if (singleAttempt(singleMessage, attemptReason))
				{
					++issued;
					continue;
				}
				if (!attemptReason.empty())
				{
					lastReason = attemptReason;
				}

				if (attemptReason == "no_money" ||
					attemptReason == "idle_worker_not_found" ||
					attemptReason == "worker_not_found" ||
					attemptReason == "black_market_prereq_missing")
				{
					break;
				}
			}

			if (issued <= 0)
			{
				reason = lastReason;
				return false;
			}
			return true;
		}

		static void mergeJsonObjectInto(nlohmann::json& dest, const nlohmann::json& src)
		{
			if (!dest.is_object())
			{
				dest = nlohmann::json::object();
			}
			if (!src.is_object())
			{
				return;
			}

			for (nlohmann::json::const_iterator it = src.begin(); it != src.end(); ++it)
			{
				dest[it.key()] = *it;
			}
		}

		static std::string normalizeBuildingMixKind(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) -> unsigned char
			{
				return static_cast<unsigned char>(std::tolower(ch));
			});
			return value;
		}

		template <typename TInferTemplate>
		bool queueAcrossProducersTotalCount(
			const nlohmann::json& message,
			const std::vector<Object*>& producers,
			Int requestedCount,
			const char* missingTemplateReason,
			std::string& reason,
			TInferTemplate&& inferTemplate)
		{
			if (producers.empty())
			{
				reason = "producer_not_found";
				return false;
			}

			Int remaining = std::max<Int>(1, requestedCount);
			Int queued = 0;
			std::string lastReason = "queue_failed";
			std::vector<bool> exhausted(producers.size(), false);

			while (remaining > 0)
			{
				bool madeProgress = false;
				bool anyCandidate = false;
				for (std::size_t idx = 0; idx < producers.size() && remaining > 0; ++idx)
				{
					if (exhausted[idx])
					{
						continue;
					}

					Object* producer = producers[idx];
					if (producer == nullptr)
					{
						exhausted[idx] = true;
						continue;
					}

					const std::string unitTemplateName = inferTemplate(producer);
					if (unitTemplateName.empty())
					{
						lastReason = missingTemplateReason;
						exhausted[idx] = true;
						continue;
					}

					anyCandidate = true;
					std::string queueReason;
					if (queueTemplateAtProducer(message, producer, unitTemplateName, queueReason))
					{
						++queued;
						--remaining;
						madeProgress = true;
						continue;
					}

					if (!queueReason.empty())
					{
						lastReason = queueReason;
					}
					if (queueReason == "queue_full" || queueReason == "no_money")
					{
						exhausted[idx] = true;
					}
				}

				if (!madeProgress)
				{
					if (!anyCandidate && lastReason == "queue_failed")
					{
						lastReason = missingTemplateReason;
					}
					break;
				}
			}

			if (queued <= 0)
			{
				reason = lastReason;
				return false;
			}
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
			const Int count = parseQueueCountArg(message);

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

			const Int templateId = unitTemplate->getTemplateID();
			const ObjectID producerId = producer->getID();
			Int queuedCount = 0;
			for (Int i = 0; i < count; ++i)
			{
				const CanMakeType canMakeNow = TheBuildAssistant->canMakeUnit(producer, unitTemplate);
				if (canMakeNow != CANMAKE_OK)
				{
					switch (canMakeNow)
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
					break;
				}

				const ProductionID productionId = production->requestUniqueUnitID();
				std::string queueReason;
				const bool queued = executeScopedSelectionCommand(player, std::vector<ObjectID>(1, producerId), queueReason, [&]() -> bool
				{
					GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_QUEUE_UNIT_CREATE);
					if (msg == nullptr)
					{
						queueReason = "message_stream_not_ready";
						return false;
					}
					msg->appendIntegerArgument(templateId);
					msg->appendIntegerArgument(static_cast<Int>(productionId));
					return true;
				});
				if (!queued)
				{
					reason = queueReason.empty() ? "unit_queue_rejected_internal" : queueReason;
					break;
				}
				++queuedCount;
			}

			if (queuedCount <= 0)
			{
				if (reason.empty())
				{
					reason = "unit_queue_rejected_internal";
				}
				return false;
			}
			return true;
		}

		bool executeGameQueueUpgrade(const nlohmann::json& message, std::string& reason)
		{
			if (TheUpgradeCenter == nullptr)
			{
				reason = "upgrade_center_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string upgradeName = canonicalizeUpgradeName(getJsonString(*argsIt, "upgrade_name"));
			if (upgradeName.empty())
			{
				reason = "missing_upgrade_name";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			const UpgradeTemplate* upgradeT = TheUpgradeCenter->findUpgrade(upgradeName.c_str());
			if (upgradeT == nullptr)
			{
				reason = "upgrade_not_found";
				return false;
			}

			Object* producer = resolveUpgradeProducerFromArgs(player, message, reason);
			if (producer == nullptr)
			{
				return false;
			}

			std::string producerKind = "any";
			if (argsIt->is_object())
			{
				producerKind = getJsonString(*argsIt, "producer_kind");
			}
			if (!messageHasExplicitProducerObjectId(message) && !producer->canProduceUpgrade(upgradeT))
			{
				Object* capableProducer = resolveUpgradeCapableProducer(player, producerKind, upgradeT);
				if (capableProducer != nullptr)
				{
					producer = capableProducer;
				}
			}

			ProductionUpdateInterface* production = producer->getProductionUpdateInterface();
			if (production == nullptr)
			{
				reason = "producer_not_factory";
				return false;
			}

			if (!producer->canProduceUpgrade(upgradeT))
			{
				reason = "producer_cannot_make_upgrade";
				return false;
			}

			const CanMakeType canQueue = production->canQueueUpgrade(upgradeT);
			if (canQueue != CANMAKE_OK)
			{
				switch (canQueue)
				{
				case CANMAKE_QUEUE_FULL:
					reason = "queue_full";
					break;
				default:
					reason = "cannot_queue_upgrade";
					break;
				}
				return false;
			}

			if (upgradeT->getUpgradeType() == UPGRADE_TYPE_PLAYER)
			{
				if (player->hasUpgradeComplete(upgradeT))
				{
					reason = "upgrade_already_complete";
					return false;
				}
				if (player->hasUpgradeInProduction(upgradeT))
				{
					reason = "upgrade_already_in_production";
					return false;
				}
				if (TheUpgradeCenter->canAffordUpgrade(player, upgradeT, FALSE) == FALSE)
				{
					reason = "no_money";
					return false;
				}
			}
			else
			{
				if (producer->hasUpgrade(upgradeT))
				{
					reason = "upgrade_already_complete";
					return false;
				}
				if (production->isUpgradeInQueue(upgradeT))
				{
					reason = "upgrade_already_in_queue";
					return false;
				}
				if (!producer->affectedByUpgrade(upgradeT))
				{
					reason = "producer_cannot_receive_upgrade";
					return false;
				}
				if (TheUpgradeCenter->canAffordUpgrade(player, upgradeT, FALSE) == FALSE)
				{
					reason = "no_money";
					return false;
				}
			}

			const ObjectID producerId = producer->getID();
			return executeScopedSelectionCommand(player, std::vector<ObjectID>(1, producerId), reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_QUEUE_UPGRADE);
				if (msg == nullptr)
				{
					reason = "message_stream_not_ready";
					return false;
				}
				msg->appendObjectIDArgument(producerId);
				msg->appendIntegerArgument(static_cast<Int>(upgradeT->getUpgradeNameKey()));
				return true;
			});
		}

		bool executeGamePurchaseScience(const nlohmann::json& message, std::string& reason)
		{
			if (TheScienceStore == nullptr)
			{
				reason = "science_store_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string scienceName = canonicalizeScienceName(getJsonString(*argsIt, "science_name"));
			if (scienceName.empty())
			{
				reason = "missing_science_name";
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

			const ScienceType science = TheScienceStore->getScienceFromInternalName(AsciiString(scienceName.c_str()));
			if (science == SCIENCE_INVALID)
			{
				reason = "science_not_found";
				return false;
			}
			if (!player->isCapableOfPurchasingScience(science))
			{
				reason = "science_not_purchasable";
				return false;
			}

			GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_PURCHASE_SCIENCE);
			if (msg == nullptr)
			{
				reason = "message_stream_not_ready";
				return false;
			}
			msg->appendIntegerArgument(static_cast<Int>(science));
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

		std::string inferRadarVanTemplateForProducer(Object* producer) const
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
				"GLAVehicleRadarVan",
				"GLARadarVan",
				"GLAVehicleRadar"
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

			for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
			{
				const std::string templateName = tt->getName().str();
				if (!containsIgnoreCase(templateName, "radar") && !containsIgnoreCase(templateName, "van"))
				{
					continue;
				}
				if (!containsIgnoreCase(templateName, "vehicle") && !containsIgnoreCase(templateName, "van"))
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
			// Callers that fan out across multiple producers already loop per requested item.
			// Force a single queue action here so count is not applied twice.
			queueArgs["count"] = 1;
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

		bool executeGameQueueRadarVansAllWarFactories(const nlohmann::json& message, std::string& reason)
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
				const std::string unitTemplateName = inferRadarVanTemplateForProducer(producer);
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

		bool executeGameQueueRadarVan(const nlohmann::json& message, std::string& reason)
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

			std::string lastReason = "radar_van_template_not_found";
			for (Object* producer : ctx.producers)
			{
				const std::string unitTemplateName = inferRadarVanTemplateForProducer(producer);
				if (unitTemplateName.empty())
				{
					continue;
				}

				std::string queueReason;
				if (queueTemplateAtProducer(message, producer, unitTemplateName, queueReason))
				{
					return true;
				}

				lastReason = queueReason;
				// Try another war factory if this one cannot currently queue.
			}

			reason = lastReason;
			return false;
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
			const Int count = parseQueueCountArg(message);
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
				for (Int i = 0; i < count; ++i)
				{
					std::string producerReason;
					if (queueWorkerAtProducer(targetProducer, producerReason))
					{
						++queuedCount;
						continue;
					}
					if (!producerReason.empty())
					{
						lastReason = producerReason;
					}
					if (producerReason == "queue_full" || producerReason == "no_money")
					{
						break;
					}
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

		bool executeGameBuildSupplyStashSmartSingle(const nlohmann::json& message, std::string& reason)
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
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

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
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

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
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

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
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

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
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

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
				return moveWorkerToPosition(worker, moveTarget, reason);
			}

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

		bool executeGameCameraSet(const nlohmann::json& message, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			bool touched = false;
			Real angle = TheTacticalView->getAngle();
			Real pitch = TheTacticalView->getPitch();
			Real zoom = TheTacticalView->getZoom();
			Real heightAboveGround = TheTacticalView->getHeightAboveGround();
			bool setHeight = false;

			const auto angleIt = argsIt->find("angle");
			if (angleIt != argsIt->end() && angleIt->is_number())
			{
				angle = angleIt->get<Real>();
				touched = true;
			}

			const auto topDownIt = argsIt->find("top_down");
			if (topDownIt != argsIt->end() && topDownIt->is_boolean() && topDownIt->get<bool>())
			{
				// ~-90 degrees in radians.
				pitch = -1.57079632679f;
				touched = true;
			}

			const auto pitchIt = argsIt->find("pitch");
			if (pitchIt != argsIt->end() && pitchIt->is_number())
			{
				pitch = pitchIt->get<Real>();
				touched = true;
			}

			const auto zoomIt = argsIt->find("zoom");
			if (zoomIt != argsIt->end() && zoomIt->is_number())
			{
				zoom = zoomIt->get<Real>();
				touched = true;
			}

			const auto zoomMulIt = argsIt->find("zoom_multiplier");
			if (zoomMulIt != argsIt->end() && zoomMulIt->is_number())
			{
				const Real mul = zoomMulIt->get<Real>();
				if (mul > 0.0f)
				{
					zoom = TheTacticalView->getZoom() * mul;
					touched = true;
				}
			}

			const auto heightIt = argsIt->find("height");
			if (heightIt != argsIt->end() && heightIt->is_number())
			{
				heightAboveGround = heightIt->get<Real>();
				setHeight = true;
				touched = true;
			}

			const auto heightMulIt = argsIt->find("height_multiplier");
			if (heightMulIt != argsIt->end() && heightMulIt->is_number())
			{
				const Real mul = heightMulIt->get<Real>();
				if (mul > 0.0f)
				{
					heightAboveGround = TheTacticalView->getHeightAboveGround() * mul;
					setHeight = true;
					touched = true;
				}
			}

			if (!touched)
			{
				reason = "no_camera_fields";
				return false;
			}

			TheTacticalView->setAngle(angle);
			TheTacticalView->setPitch(pitch);
			TheTacticalView->setZoom(zoom);
			if (setHeight)
			{
				TheTacticalView->setHeightAboveGround(heightAboveGround);
			}
			return true;
		}

		bool executeGameCameraSetZoomLimited(const nlohmann::json& message, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			bool hasValue = false;
			bool enabled = true;

			const auto enabledIt = argsIt->find("enabled");
			if (enabledIt != argsIt->end() && enabledIt->is_boolean())
			{
				enabled = enabledIt->get<bool>();
				hasValue = true;
			}

			const auto zoomLimitedIt = argsIt->find("zoom_limited");
			if (zoomLimitedIt != argsIt->end() && zoomLimitedIt->is_boolean())
			{
				enabled = zoomLimitedIt->get<bool>();
				hasValue = true;
			}

			if (!hasValue)
			{
				reason = "missing_enabled";
				return false;
			}

			TheTacticalView->setZoomLimited(enabled ? TRUE : FALSE);
			return true;
		}

		bool executeGameCameraReset(std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			TheTacticalView->setAngleAndPitchToDefault();
			TheTacticalView->setZoomToDefault();
			return true;
		}

		bool executeGameCameraLookAt(const nlohmann::json& message, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			Real x = 0.0f;
			Real y = 0.0f;
			bool hasPos = false;
			const auto xIt = argsIt->find("x");
			const auto yIt = argsIt->find("y");
			if (xIt != argsIt->end() && yIt != argsIt->end() && xIt->is_number() && yIt->is_number())
			{
				x = xIt->get<Real>();
				y = yIt->get<Real>();
				hasPos = true;
			}
			if (!hasPos)
			{
				const auto centerIt = argsIt->find("zone_center");
				if (centerIt != argsIt->end() && centerIt->is_object())
				{
					const auto cxIt = centerIt->find("x");
					const auto cyIt = centerIt->find("y");
					if (cxIt != centerIt->end() && cyIt != centerIt->end() && cxIt->is_number() && cyIt->is_number())
					{
						x = cxIt->get<Real>();
						y = cyIt->get<Real>();
						hasPos = true;
					}
				}
			}
			if (!hasPos)
			{
				reason = "missing_target_position";
				return false;
			}

			Coord3D target;
			target.x = x;
			target.y = y;
			target.z = 0.0f;
			TheTacticalView->lookAt(&target);
			return true;
		}

		bool executeGameCameraGet(const nlohmann::json& /*message*/, nlohmann::json& result, std::string& reason)
		{
			if (TheTacticalView == nullptr)
			{
				reason = "camera_not_ready";
				return false;
			}

			Coord3D pos;
			TheTacticalView->getPosition(&pos);
			result = nlohmann::json::object({
				{"path", "game.camera"},
				{"x", pos.x},
				{"y", pos.y},
				{"z", pos.z},
				{"angle", TheTacticalView->getAngle()},
				{"pitch", TheTacticalView->getPitch()},
				{"zoom", TheTacticalView->getZoom()},
				{"height_above_ground", TheTacticalView->getHeightAboveGround()},
				{"default_height", TheGlobalData != nullptr ? TheGlobalData->m_cameraHeight : 0.0f},
				{"min_height", TheGlobalData != nullptr ? TheGlobalData->m_minCameraHeight : 0.0f},
				{"max_height", TheGlobalData != nullptr ? TheGlobalData->m_maxCameraHeight : 0.0f},
				{"zoom_limited", TheTacticalView->isZoomLimited()}
			});
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
