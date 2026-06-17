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

		static bool equalsIgnoreCase(const std::string& lhs, const char* rhs)
		{
			if (rhs == nullptr)
			{
				return false;
			}

			std::string a = lhs;
			std::string b = rhs;
			std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a == b;
		}

		static bool isPalaceTemplateName(const std::string& name)
		{
			return equalsIgnoreCase(name, "GLAPalace");
		}

		static bool isCompletedStructure(const Object* obj)
		{
			if (obj == nullptr || obj->isEffectivelyDead())
			{
				return false;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return false;
			}
			if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
			{
				return false;
			}
			return true;
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
			if (obj->isKindOf(KINDOF_STRUCTURE) && obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
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
					if (producer->isKindOf(KINDOF_STRUCTURE) && producer->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					{
						reason = "producer_under_construction";
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
			if (obj->isKindOf(KINDOF_STRUCTURE) && obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
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
					if (producer->isKindOf(KINDOF_STRUCTURE) && producer->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					{
						reason = "producer_under_construction";
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
				return isPalaceTemplateName(name);
			}
			if (producerKind == "barracks")
			{
				return containsIgnoreCase(name, "barracks");
			}
			if (producerKind == "arms_dealer" || producerKind == "armsdealer" || producerKind == "war_factory")
			{
				return containsIgnoreCase(name, "arms") && containsIgnoreCase(name, "dealer");
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
			if (!isCompletedStructure(obj))
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
			if (!isCompletedStructure(obj))
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
					if (!isCompletedStructure(producer))
					{
						reason = "producer_under_construction";
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
			if (obj->isKindOf(KINDOF_STRUCTURE) && obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
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
			if (!isCompletedStructure(obj))
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

		struct NearbyStructureClearanceContext
		{
			const Coord3D* targetPos;
			Real candidateRadius;
			Real extraPadding;
			bool tooClose;
		};

		struct NearbySameTypeStructureClearanceContext
		{
			const Coord3D* targetPos;
			Real candidateRadius;
			Real extraPadding;
			const char* spacingCategory;
			bool tooClose;
		};

		static const char* getBuildSpacingCategory(const ThingTemplate* tt)
		{
			if (tt == nullptr)
			{
				return nullptr;
			}

			const std::string name = tt->getName().str();
			if (containsIgnoreCase(name, "barracks"))
			{
				return "barracks";
			}
			if (containsIgnoreCase(name, "arms") && containsIgnoreCase(name, "dealer"))
			{
				return "arms_dealer";
			}
			if (containsIgnoreCase(name, "palace"))
			{
				return "palace";
			}
			return nullptr;
		}

		static Real getSameTypeSpacingPadding(const ThingTemplate* tt)
		{
			const char* category = getBuildSpacingCategory(tt);
			if (category == nullptr)
			{
				return 0.0f;
			}

			if (std::strcmp(category, "barracks") == 0)
			{
				return 120.0f;
			}
			if (std::strcmp(category, "arms_dealer") == 0)
			{
				return 150.0f;
			}
			if (std::strcmp(category, "palace") == 0)
			{
				return 220.0f;
			}
			return 0.0f;
		}

		static void findNearbySameTypeStructureClearanceCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_STRUCTURE))
			{
				return;
			}

			NearbySameTypeStructureClearanceContext* ctx = static_cast<NearbySameTypeStructureClearanceContext*>(userData);
			if (ctx->tooClose || ctx->targetPos == nullptr || ctx->spacingCategory == nullptr)
			{
				return;
			}

			const ThingTemplate* tt = obj->getTemplate();
			if (tt == nullptr)
			{
				return;
			}
			const char* objectCategory = getBuildSpacingCategory(tt);
			if (objectCategory == nullptr || std::strcmp(objectCategory, ctx->spacingCategory) != 0)
			{
				return;
			}

			const Coord3D* pos = obj->getPosition();
			if (pos == nullptr)
			{
				return;
			}

			Real existingRadius = obj->getGeometryInfo().getBoundingCircleRadius();
			if (existingRadius < 1.0f)
			{
				existingRadius = 40.0f;
			}

			const Real required = ctx->candidateRadius + existingRadius + ctx->extraPadding;
			if (distanceSq2D(ctx->targetPos, pos) <= required * required)
			{
				ctx->tooClose = true;
			}
		}

		static bool hasOwnedSameTypeStructureTooClose(Player* player, const Coord3D* pos, const ThingTemplate* templateToPlace)
		{
			if (player == nullptr || pos == nullptr || templateToPlace == nullptr)
			{
				return false;
			}

			const char* spacingCategory = getBuildSpacingCategory(templateToPlace);
			if (spacingCategory == nullptr)
			{
				return false;
			}

			Real candidateRadius = templateToPlace->getTemplateGeometryInfo().getBoundingCircleRadius();
			if (candidateRadius < 1.0f)
			{
				candidateRadius = 40.0f;
			}

			NearbySameTypeStructureClearanceContext ctx = {
				pos,
				candidateRadius,
				getSameTypeSpacingPadding(templateToPlace),
				spacingCategory,
				false
			};
			player->iterateObjects(findNearbySameTypeStructureClearanceCallback, &ctx);
			return ctx.tooClose;
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
				// Keep the "claimed" radius tighter so a badly placed stash does not
				// accidentally mark an adjacent supply dock as already serviced.
				if (hasNearbyOwnedSupplyDropoff(player, info.source, 430.0f))
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

		Object* chooseRemoteSupplySource(
			const std::vector<SupplySourceInfo>& sources,
			const Coord3D* remoteOrigin,
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
				if (info.source == nullptr || info.source->getPosition() == nullptr || remoteOrigin == nullptr)
				{
					continue;
				}
				if (avoidSourceId > 0 && static_cast<Int>(info.source->getID()) == avoidSourceId)
				{
					continue;
				}
				const Real distSq = distanceSq2D(info.source->getPosition(), remoteOrigin);
				if (best == nullptr || distSq > bestDistSq)
				{
					best = info.source;
					bestDistSq = distSq;
				}

				if (!preferUnclaimed)
				{
					continue;
				}
				if (hasNearbyOwnedSupplyDropoff(player, info.source, 430.0f))
				{
					continue;
				}
				if (bestUnclaimed == nullptr || distSq > bestUnclaimedDistSq)
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
			const Real baseRadius = supplySource->getGeometryInfo().getBoundingCircleRadius() + 6.0f;
			const UnsignedInt legalOpts =
				BuildAssistant::TERRAIN_RESTRICTIONS |
				BuildAssistant::CLEAR_PATH |
				BuildAssistant::NO_OBJECT_OVERLAP |
				BuildAssistant::SHROUD_REVEALED;
			const Coord3D* workerPos = worker->getPosition();
			const Coord3D* commandCenterPos = nullptr;
			Object* commandCenter = findPrimaryCommandCenter(player);
			if (commandCenter != nullptr)
			{
				commandCenterPos = commandCenter->getPosition();
			}

			const Coord3D* referencePos = workerPos != nullptr ? workerPos : commandCenterPos;
			Real referenceDirX = 0.0f;
			Real referenceDirY = 0.0f;
			bool hasReferenceDir = false;
			if (referencePos != nullptr)
			{
				referenceDirX = referencePos->x - supplyPos->x;
				referenceDirY = referencePos->y - supplyPos->y;
				const Real referenceLenSq = referenceDirX * referenceDirX + referenceDirY * referenceDirY;
				if (referenceLenSq > 1.0f)
				{
					const Real invReferenceLen = 1.0f / std::sqrt(referenceLenSq);
					referenceDirX *= invReferenceLen;
					referenceDirY *= invReferenceLen;
					hasReferenceDir = true;
				}
			}

			bool found = false;
			Int bestPass = 0;
			Real bestRing = 0.0f;
			Real bestSidePenalty = 0.0f;
			Real bestWorkerDistSq = 0.0f;
			Coord3D best = *supplyPos;
			best.z = 0.0f;

			static const Real ringLimits[] = { 60.0f, 110.0f, 170.0f, 250.0f };
			for (Int pass = 0; pass < static_cast<Int>(sizeof(ringLimits) / sizeof(ringLimits[0])) && !found; ++pass)
			{
				const Real maxRing = baseRadius + ringLimits[pass];
				const bool preferReferenceSideOnly = hasReferenceDir && pass < 3;
				for (Real ring = baseRadius; ring <= maxRing; ring += 12.0f)
				{
					for (Int i = 0; i < 72; ++i)
					{
						const Real theta = static_cast<Real>(i) * (6.28318530717958647692f / 72.0f);
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
						if (hasOwnedStructureTooClose(player, &candidate, buildingTemplate, 30.0f))
						{
							continue;
						}

						Real sidePenalty = 0.0f;
						Real sideAlignment = 0.0f;
						if (hasReferenceDir)
						{
							Real candidateDirX = candidate.x - supplyPos->x;
							Real candidateDirY = candidate.y - supplyPos->y;
							const Real candidateLenSq = candidateDirX * candidateDirX + candidateDirY * candidateDirY;
							if (candidateLenSq > 1.0f)
							{
								const Real invCandidateLen = 1.0f / std::sqrt(candidateLenSq);
								candidateDirX *= invCandidateLen;
								candidateDirY *= invCandidateLen;
								sideAlignment = (candidateDirX * referenceDirX) + (candidateDirY * referenceDirY);
								sidePenalty = 1.0f - sideAlignment;
							}
						}
						if (preferReferenceSideOnly && sideAlignment < 0.65f)
						{
							continue;
						}

						const Real distSq = workerPos != nullptr ? distanceSq2D(&candidate, workerPos) : 0.0f;
						if (workerPos != nullptr)
						{
							const Real workerDist = std::sqrt(std::max<Real>(0.0f, distSq));
							if (pass < 2 && workerDist > (ring * 3.0f))
							{
								continue;
							}
						}
						if (!found ||
							pass < bestPass ||
							(pass == bestPass && ring < bestRing) ||
							(pass == bestPass && ring == bestRing && sidePenalty < bestSidePenalty) ||
							(pass == bestPass && ring == bestRing && sidePenalty == bestSidePenalty && distSq < bestWorkerDistSq))
						{
							found = true;
							bestPass = pass;
							bestRing = ring;
							bestSidePenalty = sidePenalty;
							bestWorkerDistSq = distSq;
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
				if (TheThingFactory->findTemplate(AsciiString("GLAArmsDealer"), false) != nullptr)
				{
					return "GLAArmsDealer";
				}
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
					if (!containsIgnoreCase(name, "arms") || !containsIgnoreCase(name, "dealer"))
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
					if (!isPalaceTemplateName(name))
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

		static const Coord3D* getObjectPositionOrNull(const Object* obj)
		{
			return obj != nullptr ? obj->getPosition() : nullptr;
		}

		static std::string getRequestIdForLog(const nlohmann::json& message)
		{
			return getJsonString(message, "request_id");
		}

		void logSmartBuildPlacement(
			const nlohmann::json& message,
			const char* kind,
			const char* phase,
			Player* player,
			Object* worker,
			const std::string& buildingTemplateName,
			Int requestedAnchorId,
			Object* anchor,
			const ZonePlacementArgs& zoneArgs,
			const Coord3D* target,
			Real angle,
			const char* source)
		{
			const Coord3D* workerPos = getObjectPositionOrNull(worker);
			const Coord3D* anchorPos = getObjectPositionOrNull(anchor);
			const std::string requestId = getRequestIdForLog(message);
			adapterLog(
				"smart_build_%s_%s request_id=%s player=%d worker=%d worker_pos=(%.1f,%.1f) template=%s requested_anchor=%d anchor=%d anchor_pos=(%.1f,%.1f) zone_enabled=%d zone_center=(%.1f,%.1f) zone_radius=%.1f strict_zone=%d source=%s target=(%.1f,%.1f) angle=%.3f",
				kind != nullptr ? kind : "unknown",
				phase != nullptr ? phase : "unknown",
				requestId.c_str(),
				player != nullptr ? static_cast<int>(player->getPlayerIndex()) : -1,
				worker != nullptr ? static_cast<int>(worker->getID()) : 0,
				workerPos != nullptr ? workerPos->x : 0.0f,
				workerPos != nullptr ? workerPos->y : 0.0f,
				buildingTemplateName.c_str(),
				static_cast<int>(requestedAnchorId),
				anchor != nullptr ? static_cast<int>(anchor->getID()) : 0,
				anchorPos != nullptr ? anchorPos->x : 0.0f,
				anchorPos != nullptr ? anchorPos->y : 0.0f,
				zoneArgs.hasZoneCenter ? 1 : 0,
				zoneArgs.hasZoneCenter ? zoneArgs.zoneCenter.x : 0.0f,
				zoneArgs.hasZoneCenter ? zoneArgs.zoneCenter.y : 0.0f,
				zoneArgs.zoneRadius,
				zoneArgs.strictZone ? 1 : 0,
				source != nullptr ? source : "-",
				target != nullptr ? target->x : 0.0f,
				target != nullptr ? target->y : 0.0f,
				angle);
			const bool isAutonomyRequest =
				requestId.rfind("auto_", 0) == 0
				|| requestId.rfind("auto_macro_", 0) == 0
				|| requestId.rfind("auto_prod_", 0) == 0
				|| requestId.rfind("auto_tech_", 0) == 0;
			if (isAutonomyRequest && kind != nullptr && phase != nullptr && target != nullptr)
			{
				const std::string label = std::string("build_") + kind + "_" + phase;
				recordAutonomyTelemetryEvent("build", label, source != nullptr ? source : "", target);
			}
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
					if (hasOwnedSameTypeStructureTooClose(player, &candidate, buildingTemplate))
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
			const Real preferredRadius = getPreferredBuildExpansionRadius(player, buildingTemplate, baseRadius + 240.0f);
			const Real ringStep = 24.0f;
			const Real windowSpan = 720.0f;
			const Real maxExtraRadius = 12000.0f;
			Real usedRadius = preferredRadius;

			for (Int pass = 0; pass < 3 && !found; ++pass)
			{
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
							if (hasOwnedSameTypeStructureTooClose(player, &candidate, buildingTemplate))
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
			const Coord3D* workerPosBefore = worker->getPosition();
			AIUpdateInterface* workerAiBefore = worker->getAI();
			const bool workerIdleBefore = (workerAiBefore != nullptr && workerAiBefore->isIdle());
			const bool workerBusyBefore = (workerAiBefore != nullptr && workerAiBefore->isBusy());
			adapterLog(
				"construct_issue_begin player=%d worker=%d template=%s template_id=%d worker_pos=(%.1f,%.1f) location=(%.1f,%.1f) angle=%.3f ai_idle=%d ai_busy=%d",
				static_cast<int>(player->getPlayerIndex()),
				static_cast<int>(workerId),
				buildingTemplate->getName().str(),
				static_cast<int>(templateId),
				workerPosBefore != nullptr ? workerPosBefore->x : 0.0f,
				workerPosBefore != nullptr ? workerPosBefore->y : 0.0f,
				location.x,
				location.y,
				angle,
				workerIdleBefore ? 1 : 0,
				workerBusyBefore ? 1 : 0);
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

			// For GLA workers, construction site creation takes time (worker must path to location and dig).
			// Don't check for the site immediately - trust the command was sent successfully.
			// The worker reservation and location reservation will prevent re-issuing the same command.
			const Coord3D* workerPosAfter = worker->getPosition();
			AIUpdateInterface* workerAiAfter = worker->getAI();
			const bool workerIdleAfter = (workerAiAfter != nullptr && workerAiAfter->isIdle());
			const bool workerBusyAfter = (workerAiAfter != nullptr && workerAiAfter->isBusy());
			adapterLog(
				"construct_issue_result player=%d worker=%d template=%s worker_pos=(%.1f,%.1f) ai_idle=%d ai_busy=%d command_sent=1",
				static_cast<int>(player->getPlayerIndex()),
				static_cast<int>(workerId),
				buildingTemplate->getName().str(),
				workerPosAfter != nullptr ? workerPosAfter->x : 0.0f,
				workerPosAfter != nullptr ? workerPosAfter->y : 0.0f,
				workerIdleAfter ? 1 : 0,
				workerBusyAfter ? 1 : 0);

			// Keep a worker out of selection for a while after issuing construction.
			// This avoids repeatedly interrupting the same builder if AI idle/busy flags
			// lag for a few frames or temporarily report idle.
			reserveWorkerForBuild(worker, 45000u);
			reserveBuildLocation(player, location, buildingTemplate);

			// Phase 6.1: Create special task reservation for build lifecycle tracking
			const unsigned int taskId = m_autonomy.taskReservationManager.createReservation(
				SpecialTaskType::BuildStructure,
				workerId,
				buildingTemplate->getName().str(),
				location,
				"smart_build",
				90000u); // 90 second timeout
			adapterLog(
				"special_task_assigned task=%u type=BuildStructure owner=smart_build source=%u template=%s x=%.1f y=%.1f",
				taskId,
				static_cast<unsigned int>(workerId),
				buildingTemplate->getName().str(),
				location.x,
				location.y);

			// Telemetry event for task assignment
			recordAutonomyTelemetryEvent(
				"construction_task",
				buildingTemplate->getName().str(),
				"task_assigned",
				&location);

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

		bool executeGameDebugDeshroud(const nlohmann::json& message, std::string& reason)
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
				reason = "deshroud_not_allowed_in_multiplayer";
				return false;
			}

			if (ThePartitionManager == nullptr)
			{
				reason = "partition_manager_not_ready";
				return false;
			}

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			ThePartitionManager->revealMapForPlayerPermanently(player->getPlayerIndex());
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
					attemptReason == "black_market_prereq_missing" ||
					attemptReason == "construct_site_not_created")
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
			const bool requireStructuredProducerKind =
				producerKind == "barracks"
				|| producerKind == "arms_dealer"
				|| producerKind == "armsdealer"
				|| producerKind == "war_factory"
				|| producerKind == "palace"
				|| producerKind == "black_market"
				|| producerKind == "blackmarket"
				|| producerKind == "market";

			Player* player = resolvePlayerFromArgs(message, reason);
			if (player == nullptr)
			{
				return false;
			}

			Object* producer = requireStructuredProducerKind
				? resolveUpgradeProducerFromArgs(player, message, reason)
				: resolveProducerFromArgs(player, message, requireCommandCenter, reason);
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

			const ThingTemplate* producerTemplate = producer->getTemplate();
			const std::string producerTemplateName = producerTemplate != nullptr ? producerTemplate->getName().str() : std::string();
			if (containsIgnoreCase(unitTemplateName, "scudlauncher"))
			{
				adapterLog(
					"queue_unit_attempt player=%d producer_id=%d producer_template=%s producer_kind=%s unit_template=%s",
					player->getPlayerIndex(),
					static_cast<Int>(producer->getID()),
					producerTemplateName.c_str(),
					producerKind.c_str(),
					unitTemplateName.c_str());
			}

			const CanMakeType canMake = TheBuildAssistant->canMakeUnit(producer, unitTemplate);
			if (canMake != CANMAKE_OK)
			{
				adapterLog(
					"queue_unit_prereq_probe player=%d producer_id=%d producer_template=%s producer_kind=%s unit_template=%s can_make=%d",
					player->getPlayerIndex(),
					static_cast<Int>(producer->getID()),
					producerTemplateName.c_str(),
					producerKind.c_str(),
					unitTemplateName.c_str(),
					static_cast<Int>(canMake));
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

			const ThingTemplate* producerTemplate = producer->getTemplate();
			const std::string producerTemplateName = producerTemplate != nullptr ? producerTemplate->getName().str() : std::string();
			adapterLog(
				"queue_upgrade_attempt player=%d producer_id=%d producer_template=%s producer_kind=%s upgrade=%s",
				player->getPlayerIndex(),
				static_cast<Int>(producer->getID()),
				producerTemplateName.c_str(),
				producerKind.c_str(),
				upgradeName.c_str());

			ProductionUpdateInterface* production = producer->getProductionUpdateInterface();
			if (production == nullptr)
			{
				adapterLog(
					"queue_upgrade_probe player=%d producer_id=%d producer_template=%s producer_kind=%s upgrade=%s producer_not_factory=1",
					player->getPlayerIndex(),
					static_cast<Int>(producer->getID()),
					producerTemplateName.c_str(),
					producerKind.c_str(),
					upgradeName.c_str());
				reason = "producer_not_factory";
				return false;
			}

			if (!producer->canProduceUpgrade(upgradeT))
			{
				adapterLog(
					"queue_upgrade_probe player=%d producer_id=%d producer_template=%s producer_kind=%s upgrade=%s can_produce=0",
					player->getPlayerIndex(),
					static_cast<Int>(producer->getID()),
					producerTemplateName.c_str(),
					producerKind.c_str(),
					upgradeName.c_str());
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

			std::vector<std::string> candidates;
			const std::string side = player->getSide().str();
			const std::string baseSide = player->getBaseSide().str();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (containsIgnoreCase(side, "slth") || containsIgnoreCase(side, "stealth"))
				{
					candidates.push_back("GC_Slth_GLAInfantryRebel");
				}
				if (containsIgnoreCase(side, "chem") || containsIgnoreCase(side, "toxin"))
				{
					candidates.push_back("GC_Chem_GLAInfantryRebel");
				}
				if (containsIgnoreCase(side, "demo"))
				{
					candidates.push_back("Demo_GLAInfantryRebel");
				}
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
				if (isPotentiallyQueueable(tt))
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

			std::vector<std::string> candidates;
			const Player* player = producer->getControllingPlayer();
			const std::string side = player != nullptr ? player->getSide().str() : std::string();
			const std::string baseSide = player != nullptr ? player->getBaseSide().str() : std::string();
			if (containsIgnoreCase(side, "gla") || containsIgnoreCase(baseSide, "gla"))
			{
				if (containsIgnoreCase(side, "slth") || containsIgnoreCase(side, "stealth"))
				{
					candidates.push_back("GC_Slth_GLAVehicleQuadCannon");
				}
				if (containsIgnoreCase(side, "chem") || containsIgnoreCase(side, "toxin"))
				{
					candidates.push_back("GC_Chem_GLAVehicleQuadCannon");
				}
				if (containsIgnoreCase(side, "demo"))
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

		std::string inferRocketBuggyTemplateForProducer(Object* producer) const
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
				"GLAVehicleRocketBuggy"
			};
			for (const char* name : candidates)
			{
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
				if (tt != nullptr && isPotentiallyQueueable(tt))
				{
					return name;
				}
			}
			for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
			{
				const std::string templateName = tt->getName().str();
				if (containsIgnoreCase(templateName, "rocketbuggy") && isPotentiallyQueueable(tt))
				{
					return templateName;
				}
			}
			return std::string();
		}

		std::string inferTechnicalTemplateForProducer(Object* producer) const
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
				"GLAVehicleTechnical"
			};
			for (const char* name : candidates)
			{
				const ThingTemplate* tt = TheThingFactory->findTemplate(AsciiString(name), false);
				if (tt != nullptr && isPotentiallyQueueable(tt))
				{
					return name;
				}
			}
			for (const ThingTemplate* tt = TheThingFactory->firstTemplate(); tt != nullptr; tt = tt->friend_getNextTemplate())
			{
				const std::string templateName = tt->getName().str();
				if (containsIgnoreCase(templateName, "technical") && isPotentiallyQueueable(tt))
				{
					return templateName;
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
					lastReason = "soldier_template_not_found";
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

		bool executeGameQueueRocketBuggiesAllWarFactories(const nlohmann::json& message, std::string& reason)
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
				const std::string unitTemplateName = inferRocketBuggyTemplateForProducer(producer);
				if (unitTemplateName.empty())
				{
					lastReason = "rocket_buggy_template_not_found";
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

			const Coord3D* workerPos = worker->getPosition();
			const Coord3D* supplyPos = selectedSupply != nullptr ? selectedSupply->getPosition() : nullptr;
			const std::string requestId = getRequestIdForLog(message);
			Coord3D location;
			Real angle = 0.0f;
			if (!findBuildLocationNearSupply(player, worker, selectedSupply, buildingTemplate, location, angle))
			{
				adapterLog(
					"supply_find_location_failed request_id=%s player=%d worker=%d worker_pos=(%.1f,%.1f) supply=%d supply_pos=(%.1f,%.1f) template=%s reason=%s",
					requestId.c_str(),
					static_cast<int>(player->getPlayerIndex()),
					static_cast<int>(worker->getID()),
					workerPos != nullptr ? workerPos->x : 0.0f,
					workerPos != nullptr ? workerPos->y : 0.0f,
					selectedSupply != nullptr ? static_cast<int>(selectedSupply->getID()) : 0,
					supplyPos != nullptr ? supplyPos->x : 0.0f,
					supplyPos != nullptr ? supplyPos->y : 0.0f,
					buildingTemplateName.c_str(),
					"no_legal_build_location");
				reason = "no_legal_build_location";
				return false;
			}

			adapterLog(
				"supply_find_location_ok request_id=%s player=%d worker=%d worker_pos=(%.1f,%.1f) requested_supply=%d selected_supply=%d supply_pos=(%.1f,%.1f) template=%s location=(%.1f,%.1f) angle=%.3f",
				requestId.c_str(),
				static_cast<int>(player->getPlayerIndex()),
				static_cast<int>(worker->getID()),
				workerPos != nullptr ? workerPos->x : 0.0f,
				workerPos != nullptr ? workerPos->y : 0.0f,
				static_cast<int>(requestedSupplyId),
				selectedSupply != nullptr ? static_cast<int>(selectedSupply->getID()) : 0,
				supplyPos != nullptr ? supplyPos->x : 0.0f,
				supplyPos != nullptr ? supplyPos->y : 0.0f,
				buildingTemplateName.c_str(),
				location.x,
				location.y,
				angle);

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

		#include "AIControlAdapterGameActionsSmartBuild.inl"

		#include "AIControlAdapterGameActionsCombat.inl"

		#include "AIControlAdapterGameActionsCamera.inl"

		#include "AIControlAdapterGameActionsOrders.inl"
