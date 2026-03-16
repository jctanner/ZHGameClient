		nlohmann::json buildPlayerDetails(Player* player) const
		{
			return nlohmann::json{
				{"player", buildLocalPlayerSummary(player)},
				{"resources", buildResourcesSummary(player)},
				{"units", buildUnitCountsSummary(player)}
			};
		}

		nlohmann::json buildResourcesSummary(const Player* player) const
		{
			UnsignedInt money = 0u;
			const Money* wallet = player->getMoney();
			if (wallet != nullptr)
			{
				money = wallet->countMoney();
			}

			return nlohmann::json{
				{"money", money},
				{"skill_points", player->getSkillPoints()},
				{"science_purchase_points", player->getSciencePurchasePoints()},
				{"rank_level", player->getRankLevel()}
			};
		}

		nlohmann::json buildUnitCountsSummary(Player* player) const
		{
			const KindOfMaskType none = KINDOFMASK_NONE;
			const KindOfMaskType structureMask = MAKE_KINDOF_MASK(KINDOF_STRUCTURE);
			const KindOfMaskType infantryMask = MAKE_KINDOF_MASK(KINDOF_INFANTRY);
			const KindOfMaskType vehicleMask = MAKE_KINDOF_MASK(KINDOF_VEHICLE);
			const KindOfMaskType aircraftMask = MAKE_KINDOF_MASK(KINDOF_AIRCRAFT);
			const KindOfMaskType dozerMask = MAKE_KINDOF_MASK(KINDOF_DOZER);
			const KindOfMaskType harvesterMask = MAKE_KINDOF_MASK(KINDOF_HARVESTER);

			return nlohmann::json{
				{"buildings", player->countBuildings()},
				{"units_total", player->countObjects(none, structureMask)},
				{"infantry", player->countObjects(infantryMask, none)},
				{"vehicles", player->countObjects(vehicleMask, none)},
				{"aircraft", player->countObjects(aircraftMask, none)},
				{"dozers", player->countObjects(dozerMask, none)},
				{"harvesters", player->countObjects(harvesterMask, none)},
				{"objects_total", player->countObjects(none, none)}
			};
		}

		static const char* classifyObjectClass(const Object* obj)
		{
			if (obj == nullptr)
			{
				return "unknown";
			}
			if (obj->isKindOf(KINDOF_STRUCTURE))
			{
				return "building";
			}
			if (obj->isKindOf(KINDOF_INFANTRY))
			{
				return "infantry";
			}
			if (obj->isKindOf(KINDOF_VEHICLE))
			{
				return "vehicle";
			}
			if (obj->isKindOf(KINDOF_AIRCRAFT))
			{
				return "aircraft";
			}
			return "unit";
		}

		static bool isCivilianLikePlayer(const Player* player)
		{
			if (player == nullptr)
			{
				return true;
			}

			auto toLower = [](std::string value) -> std::string
			{
				std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) -> unsigned char
				{
					return static_cast<unsigned char>(std::tolower(ch));
				});
				return value;
			};

			const std::string side = toLower(player->getSide().str());
			const std::string baseSide = toLower(player->getBaseSide().str());
			const std::string keyName = toLower(KEYNAME(player->getPlayerNameKey()).str());
			const std::string displayName = toLower(unicodeToUtf8(const_cast<Player*>(player)->getPlayerDisplayName()));

			auto hasCivilianTag = [](const std::string& s) -> bool
			{
				return s.find("civilian") != std::string::npos;
			};

			return hasCivilianTag(side) || hasCivilianTag(baseSide) || hasCivilianTag(keyName) || hasCivilianTag(displayName);
		}

		nlohmann::json buildObjectSummaryRow(const Object* obj, bool includeIdleState) const
		{
			nlohmann::json row = nlohmann::json::object();
			if (obj == nullptr)
			{
				return row;
			}

			const Coord3D* pos = obj->getPosition();
			row["id"] = obj->getID();
			row["template"] = obj->getTemplate() != nullptr ? obj->getTemplate()->getName().str() : "";
			row["class"] = classifyObjectClass(obj);
			row["x"] = pos != nullptr ? pos->x : 0.0f;
			row["y"] = pos != nullptr ? pos->y : 0.0f;
			row["z"] = pos != nullptr ? pos->z : 0.0f;
			row["under_construction"] = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
			if (includeIdleState)
			{
				const AIUpdateInterface* ai = obj->getAI();
				row["idle"] = ai != nullptr ? ai->isIdle() : false;
			}
			return row;
		}

		struct OwnedObjectCollectContext
		{
			AIControlAdapterState* self;
			nlohmann::json* units;
			nlohmann::json* buildings;
		};

		static void collectOwnedObjectsCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}
			OwnedObjectCollectContext* ctx = static_cast<OwnedObjectCollectContext*>(userData);
			nlohmann::json row = ctx->self->buildObjectSummaryRow(obj, true);
			if (obj->isKindOf(KINDOF_STRUCTURE))
			{
				ctx->buildings->push_back(row);
			}
			else
			{
				ctx->units->push_back(row);
			}
		}

		nlohmann::json buildOwnedObjectsSummary(Player* player)
		{
			nlohmann::json units = nlohmann::json::array();
			nlohmann::json buildings = nlohmann::json::array();
			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"units", units},
					{"buildings", buildings}
				};
			}

			OwnedObjectCollectContext ctx = { this, &units, &buildings };
			player->iterateObjects(collectOwnedObjectsCallback, &ctx);

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"units", units},
				{"buildings", buildings}
			};
		}

		nlohmann::json buildObjectMapRow(const Object* obj, bool includeTemplateAndIdle = false) const
		{
			nlohmann::json row = nlohmann::json::object();
			if (obj == nullptr)
			{
				return row;
			}
			const Coord3D* pos = obj->getPosition();
			row["id"] = obj->getID();
			row["class"] = classifyObjectClass(obj);
			row["x"] = pos != nullptr ? pos->x : 0.0f;
			row["y"] = pos != nullptr ? pos->y : 0.0f;
			row["under_construction"] = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
			if (includeTemplateAndIdle)
			{
				row["template"] = obj->getTemplate() != nullptr ? obj->getTemplate()->getName().str() : "";
				const AIUpdateInterface* ai = obj->getAI();
				row["idle"] = ai != nullptr ? ai->isIdle() : false;
			}
			return row;
		}

		void refreshOwnedObjectCache(Player* player)
		{
			m_ownedCacheUnits = nlohmann::json::array();
			m_ownedCacheBuildings = nlohmann::json::array();
			m_ownedCacheIdleWorkers = nlohmann::json::array();
			m_ownedCacheUnitsTotal = 0;
			m_ownedCacheBuildingsTotal = 0;
			m_ownedCacheIdleWorkersTotal = 0;

			if (player == nullptr)
			{
				m_ownedCacheValid = true;
				m_ownedCachePlayerIndex = -1;
				++m_ownedCacheVersion;
				m_ownedCacheLastRefreshTick = ::GetTickCount();
				return;
			}

			struct OwnedCacheCollectContext
			{
				AIControlAdapterState* self;
			};

			OwnedCacheCollectContext ctx = { this };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}

				OwnedCacheCollectContext* ctx = static_cast<OwnedCacheCollectContext*>(userData);
				AIControlAdapterState* self = ctx->self;
				const bool isStructure = obj->isKindOf(KINDOF_STRUCTURE);
				nlohmann::json row = self->buildObjectMapRow(obj, true);
				if (isStructure)
				{
					++self->m_ownedCacheBuildingsTotal;
					self->m_ownedCacheBuildings.push_back(row);
					return;
				}

				++self->m_ownedCacheUnitsTotal;
				self->m_ownedCacheUnits.push_back(row);

				const ThingTemplate* tt = obj->getTemplate();
				if (tt == nullptr)
				{
					return;
				}
				const std::string name = tt->getName().str();
				if (!containsIgnoreCase(name, "worker") && !containsIgnoreCase(name, "dozer"))
				{
					return;
				}
				const AIUpdateInterface* ai = obj->getAI();
				if (ai == nullptr || !ai->isIdle())
				{
					return;
				}

				++self->m_ownedCacheIdleWorkersTotal;
				self->m_ownedCacheIdleWorkers.push_back(row);
			}, &ctx);

			m_ownedCacheValid = true;
			m_ownedCachePlayerIndex = player->getPlayerIndex();
			++m_ownedCacheVersion;
			m_ownedCacheLastRefreshTick = ::GetTickCount();
		}

		void ensureOwnedObjectCache(Player* player, DWORD maxCacheAgeMs = 60000u)
		{
			const Int playerIndex = (player != nullptr) ? player->getPlayerIndex() : -1;
			const DWORD nowTick = ::GetTickCount();
			if (m_ownedCacheValid && m_ownedCachePlayerIndex == playerIndex)
			{
				const DWORD age = nowTick - m_ownedCacheLastRefreshTick;
				if (age <= maxCacheAgeMs)
				{
					return;
				}
			}
			refreshOwnedObjectCache(player);
		}

		static nlohmann::json truncateJsonArray(const nlohmann::json& source, std::size_t maxItems)
		{
			nlohmann::json out = nlohmann::json::array();
			if (!source.is_array())
			{
				return out;
			}
			const std::size_t count = std::min<std::size_t>(source.size(), maxItems);
			for (std::size_t i = 0; i < count; ++i)
			{
				out.push_back(source[i]);
			}
			return out;
		}

		nlohmann::json buildOwnedObjectsSummaryCompact(Player* player, std::size_t maxUnits, std::size_t maxBuildings)
		{
			nlohmann::json units = nlohmann::json::array();
			nlohmann::json buildings = nlohmann::json::array();
			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"units", units},
					{"buildings", buildings},
					{"units_total", 0},
					{"buildings_total", 0}
				};
			}

			struct CompactCollectContext
			{
				AIControlAdapterState* self;
				nlohmann::json* units;
				nlohmann::json* buildings;
				std::size_t maxUnits;
				std::size_t maxBuildings;
				Int totalUnits;
				Int totalBuildings;
			};

			CompactCollectContext ctx = { this, &units, &buildings, maxUnits, maxBuildings, 0, 0 };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				CompactCollectContext* ctx = static_cast<CompactCollectContext*>(userData);
				nlohmann::json row = ctx->self->buildObjectMapRow(obj, true);
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					++ctx->totalBuildings;
					if (ctx->buildings->size() < ctx->maxBuildings)
					{
						ctx->buildings->push_back(row);
					}
				}
				else
				{
					++ctx->totalUnits;
					if (ctx->units->size() < ctx->maxUnits)
					{
						ctx->units->push_back(row);
					}
				}
			}, &ctx);

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"units", units},
				{"buildings", buildings},
				{"units_total", ctx.totalUnits},
				{"buildings_total", ctx.totalBuildings}
			};
		}

		nlohmann::json buildOwnedObjectsUnitsMap(Player* player, std::size_t maxUnits)
		{
			ensureOwnedObjectCache(player);
			nlohmann::json units = truncateJsonArray(m_ownedCacheUnits, maxUnits);
			const Int playerIndex = (player != nullptr) ? player->getPlayerIndex() : -1;
			const Int totalUnits = (player != nullptr) ? m_ownedCacheUnitsTotal : 0;

			return nlohmann::json{
				{"player_index", playerIndex},
				{"units", units},
				{"units_total", totalUnits},
				{"truncated", units.size() < static_cast<std::size_t>(totalUnits)},
				{"path", "game.objects_units_map"},
				{"cache_version", m_ownedCacheVersion},
				{"cache_age_ms", ::GetTickCount() - m_ownedCacheLastRefreshTick}
			};
		}

		nlohmann::json buildOwnedObjectsBuildingsMap(Player* player, std::size_t maxBuildings)
		{
			ensureOwnedObjectCache(player);
			nlohmann::json buildings = truncateJsonArray(m_ownedCacheBuildings, maxBuildings);
			const Int playerIndex = (player != nullptr) ? player->getPlayerIndex() : -1;
			const Int totalBuildings = (player != nullptr) ? m_ownedCacheBuildingsTotal : 0;

			return nlohmann::json{
				{"player_index", playerIndex},
				{"buildings", buildings},
				{"buildings_total", totalBuildings},
				{"truncated", buildings.size() < static_cast<std::size_t>(totalBuildings)},
				{"path", "game.objects_buildings_map"},
				{"cache_version", m_ownedCacheVersion},
				{"cache_age_ms", ::GetTickCount() - m_ownedCacheLastRefreshTick}
			};
		}

		nlohmann::json buildIdleWorkersSummary(Player* player, std::size_t maxWorkers)
		{
			ensureOwnedObjectCache(player);
			nlohmann::json workers = truncateJsonArray(m_ownedCacheIdleWorkers, maxWorkers);
			const Int playerIndex = (player != nullptr) ? player->getPlayerIndex() : -1;
			const Int totalIdleWorkers = (player != nullptr) ? m_ownedCacheIdleWorkersTotal : 0;

			return nlohmann::json{
				{"player_index", playerIndex},
				{"workers", workers},
				{"idle_workers_total", totalIdleWorkers},
				{"truncated", workers.size() < static_cast<std::size_t>(totalIdleWorkers)},
				{"path", "game.idle_workers"},
				{"cache_version", m_ownedCacheVersion},
				{"cache_age_ms", ::GetTickCount() - m_ownedCacheLastRefreshTick}
			};
		}

		nlohmann::json buildZoneCountsSummary(Player* player, const Coord3D* zoneCenter, Real zoneRadius)
		{
			nlohmann::json counts = nlohmann::json::object({
				{"command_centers", 0},
				{"stashes", 0},
				{"barracks", 0},
				{"arms_dealers", 0},
				{"palaces", 0},
				{"black_markets", 0},
				{"tunnel_networks", 0},
				{"stinger_sites", 0},
				{"other_buildings", 0},
				{"units", 0},
				{"buildings", 0}
			});

			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"counts", counts},
					{"zone", nlohmann::json::object({
						{"has_center", false},
						{"radius", zoneRadius}
					})},
					{"path", "game.zone_counts"}
				};
			}

			struct ZoneCountContext
			{
				const Coord3D* zoneCenter;
				Real radiusSq;
				nlohmann::json* counts;
			};
			ZoneCountContext ctx = { zoneCenter, zoneRadius * zoneRadius, &counts };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				ZoneCountContext* ctx = static_cast<ZoneCountContext*>(userData);
				const Coord3D* pos = obj->getPosition();
				if (ctx->zoneCenter != nullptr && pos != nullptr)
				{
					const Real dx = pos->x - ctx->zoneCenter->x;
					const Real dy = pos->y - ctx->zoneCenter->y;
					if ((dx * dx + dy * dy) > ctx->radiusSq)
					{
						return;
					}
				}

				nlohmann::json& counts = *ctx->counts;
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					counts["buildings"] = counts["buildings"].get<Int>() + 1;
					const ThingTemplate* tt = obj->getTemplate();
					const std::string name = tt != nullptr ? tt->getName().str() : "";
					if (containsIgnoreCase(name, "commandcenter"))
					{
						counts["command_centers"] = counts["command_centers"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "supplystash") || containsIgnoreCase(name, "supplycenter"))
					{
						counts["stashes"] = counts["stashes"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "barracks"))
					{
						counts["barracks"] = counts["barracks"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "armsdealer") || containsIgnoreCase(name, "warfactory"))
					{
						counts["arms_dealers"] = counts["arms_dealers"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "palace"))
					{
						counts["palaces"] = counts["palaces"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "blackmarket"))
					{
						counts["black_markets"] = counts["black_markets"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "tunnelnetwork"))
					{
						counts["tunnel_networks"] = counts["tunnel_networks"].get<Int>() + 1;
					}
					else if (containsIgnoreCase(name, "stingersite"))
					{
						counts["stinger_sites"] = counts["stinger_sites"].get<Int>() + 1;
					}
					else
					{
						counts["other_buildings"] = counts["other_buildings"].get<Int>() + 1;
					}
				}
				else
				{
					counts["units"] = counts["units"].get<Int>() + 1;
				}
			}, &ctx);

			nlohmann::json zone = nlohmann::json::object({
				{"has_center", zoneCenter != nullptr},
				{"radius", zoneRadius}
			});
			if (zoneCenter != nullptr)
			{
				zone["x"] = zoneCenter->x;
				zone["y"] = zoneCenter->y;
			}

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"counts", counts},
				{"zone", zone},
				{"path", "game.zone_counts"}
			};
		}

		nlohmann::json buildAllPlayersObjectsSummaryCompact(std::size_t maxUnitsPerPlayer, std::size_t maxBuildingsPerPlayer)
		{
			nlohmann::json players = nlohmann::json::array();
			if (ThePlayerList == nullptr)
			{
				return nlohmann::json{
					{"count", 0},
					{"players", players}
				};
			}

			const Player* neutralPlayer = ThePlayerList->getNeutralPlayer();
			const Int count = ThePlayerList->getPlayerCount();
			for (Int i = 0; i < count; ++i)
			{
				Player* player = ThePlayerList->getNthPlayer(i);
				if (player == nullptr)
				{
					continue;
				}
				if (neutralPlayer != nullptr && player == neutralPlayer)
				{
					continue;
				}
				if (isCivilianLikePlayer(player))
				{
					continue;
				}

				nlohmann::json row = buildOwnedObjectsSummaryCompact(player, maxUnitsPerPlayer, maxBuildingsPerPlayer);
				row["player"] = buildLocalPlayerSummary(player);
				players.push_back(row);
			}

			return nlohmann::json{
				{"count", players.size()},
				{"players", players}
			};
		}

		nlohmann::json buildAllPlayersObjectsSummary()
		{
			nlohmann::json players = nlohmann::json::array();
			if (ThePlayerList == nullptr)
			{
				return nlohmann::json{
					{"count", 0},
					{"players", players}
				};
			}

			const Player* neutralPlayer = ThePlayerList->getNeutralPlayer();
			const Int count = ThePlayerList->getPlayerCount();
			for (Int i = 0; i < count; ++i)
			{
				Player* player = ThePlayerList->getNthPlayer(i);
				if (player == nullptr)
				{
					continue;
				}
				if (neutralPlayer != nullptr && player == neutralPlayer)
				{
					continue;
				}
				if (isCivilianLikePlayer(player))
				{
					continue;
				}

				nlohmann::json row = buildOwnedObjectsSummary(player);
				row["player"] = buildLocalPlayerSummary(player);
				players.push_back(row);
			}

			return nlohmann::json{
				{"count", players.size()},
				{"players", players}
			};
		}

		nlohmann::json buildVisibleEnemiesSummary(Player* localPlayer)
		{
			nlohmann::json enemies = nlohmann::json::array();
			if (localPlayer == nullptr || ThePlayerList == nullptr)
			{
				return nlohmann::json{
					{"count", 0},
					{"enemies", enemies}
				};
			}

			const Int playerCount = ThePlayerList->getPlayerCount();
			for (Int i = 0; i < playerCount; ++i)
			{
				Player* enemyPlayer = ThePlayerList->getNthPlayer(i);
				if (enemyPlayer == nullptr || enemyPlayer == localPlayer)
				{
					continue;
				}
				if (enemyPlayer == ThePlayerList->getNeutralPlayer())
				{
					continue;
				}

				struct EnemyObjectCollectContext
				{
					AIControlAdapterState* self;
					Player* localPlayer;
					nlohmann::json* enemies;
				};
				EnemyObjectCollectContext ctx = { this, localPlayer, &enemies };

				enemyPlayer->iterateObjects([](Object* obj, void* userData)
				{
					if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
					{
						return;
					}
					EnemyObjectCollectContext* ctx = static_cast<EnemyObjectCollectContext*>(userData);
					if (!obj->isLogicallyVisible())
					{
						return;
					}
					nlohmann::json row = ctx->self->buildObjectSummaryRow(obj, false);
					Player* owner = obj->getControllingPlayer();
					row["player_index"] = owner != nullptr ? owner->getPlayerIndex() : -1;
					ctx->enemies->push_back(row);
				}, &ctx);
			}

			return nlohmann::json{
				{"count", enemies.size()},
				{"enemies", enemies}
			};
		}

		bool executeGameQuery(const nlohmann::json& message, nlohmann::json& result, std::string& reason)
		{
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

			std::string path = "game.status";
			bool hasPlayerIndex = false;
			Int requestedPlayerIndex = -1;
			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const std::string requestedPath = getJsonString(*argsIt, "path");
				if (!requestedPath.empty())
				{
					path = requestedPath;
				}

				const auto playerIndexIt = argsIt->find("player_index");
				if (playerIndexIt != argsIt->end() && playerIndexIt->is_number_integer())
				{
					hasPlayerIndex = true;
					requestedPlayerIndex = playerIndexIt->get<Int>();
				}
			}

			Player* selectedPlayer = localPlayer;
			if (hasPlayerIndex)
			{
				selectedPlayer = getPlayerByIndex(requestedPlayerIndex);
				if (selectedPlayer == nullptr)
				{
					reason = "player_not_found";
					return false;
				}
			}

			if (path == "game.local_player")
			{
				result = buildLocalPlayerSummary(localPlayer);
				return true;
			}

			if (path == "game.resources")
			{
				result = buildResourcesSummary(selectedPlayer);
				return true;
			}

			if (path == "game.faction")
			{
				const PlayerTemplate* playerTemplate = selectedPlayer->getPlayerTemplate();
				result = nlohmann::json{
					{"side", selectedPlayer->getSide().str()},
					{"base_side", selectedPlayer->getBaseSide().str()},
					{"template_name", playerTemplate != nullptr ? playerTemplate->getName().str() : ""},
					{"template_side", playerTemplate != nullptr ? playerTemplate->getSide().str() : ""},
					{"template_base_side", playerTemplate != nullptr ? playerTemplate->getBaseSide().str() : ""},
					{"player_index", selectedPlayer->getPlayerIndex()}
				};
				return true;
			}

			if (path == "game.units")
			{
				result = buildUnitCountsSummary(selectedPlayer);
				result["player_index"] = selectedPlayer->getPlayerIndex();
				return true;
			}

			if (path == "game.objects")
			{
				result = buildOwnedObjectsSummary(selectedPlayer);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.objects_map")
			{
				result = buildOwnedObjectsSummaryCompact(selectedPlayer, 60u, 60u);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				result["truncated"] = true;
				result["path"] = "game.objects_map";
				return true;
			}

			if (path == "game.objects_cache_status")
			{
				const Int selectedIndex = selectedPlayer != nullptr ? selectedPlayer->getPlayerIndex() : -1;
				const bool validForSelected = m_ownedCacheValid && (m_ownedCachePlayerIndex == selectedIndex);
				result = nlohmann::json{
					{"player_index", selectedIndex},
					{"cache_valid", validForSelected},
					{"cache_player_index", m_ownedCachePlayerIndex},
					{"cache_version", m_ownedCacheVersion},
					{"cache_age_ms", validForSelected ? (::GetTickCount() - m_ownedCacheLastRefreshTick) : 0u},
					{"units_total", validForSelected ? m_ownedCacheUnitsTotal : 0},
					{"buildings_total", validForSelected ? m_ownedCacheBuildingsTotal : 0},
					{"idle_workers_total", validForSelected ? m_ownedCacheIdleWorkersTotal : 0},
					{"path", "game.objects_cache_status"}
				};
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.objects_cache_refresh")
			{
				refreshOwnedObjectCache(selectedPlayer);
				result = nlohmann::json{
					{"player_index", selectedPlayer != nullptr ? selectedPlayer->getPlayerIndex() : -1},
					{"cache_version", m_ownedCacheVersion},
					{"cache_age_ms", 0u},
					{"units_total", m_ownedCacheUnitsTotal},
					{"buildings_total", m_ownedCacheBuildingsTotal},
					{"idle_workers_total", m_ownedCacheIdleWorkersTotal},
					{"path", "game.objects_cache_refresh"}
				};
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.zone_counts")
			{
				Coord3D zoneCenter;
				zoneCenter.x = 0.0f;
				zoneCenter.y = 0.0f;
				zoneCenter.z = 0.0f;
				bool hasZoneCenter = false;
				Real zoneRadius = 600.0f;
				if (argsIt != message.end() && argsIt->is_object())
				{
					const auto radiusIt = argsIt->find("zone_radius");
					if (radiusIt != argsIt->end() && radiusIt->is_number())
					{
						zoneRadius = std::max<Real>(64.0f, radiusIt->get<Real>());
					}
					const auto centerIt = argsIt->find("zone_center");
					if (centerIt != argsIt->end() && centerIt->is_object())
					{
						const auto cxIt = centerIt->find("x");
						const auto cyIt = centerIt->find("y");
						if (cxIt != centerIt->end() && cyIt != centerIt->end() && cxIt->is_number() && cyIt->is_number())
						{
							zoneCenter.x = cxIt->get<Real>();
							zoneCenter.y = cyIt->get<Real>();
							zoneCenter.z = 0.0f;
							hasZoneCenter = true;
						}
					}
				}
				result = buildZoneCountsSummary(selectedPlayer, hasZoneCenter ? &zoneCenter : nullptr, zoneRadius);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.objects_units_map")
			{
				result = buildOwnedObjectsUnitsMap(selectedPlayer, 120u);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.objects_buildings_map")
			{
				result = buildOwnedObjectsBuildingsMap(selectedPlayer, 120u);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.idle_workers")
			{
				result = buildIdleWorkersSummary(selectedPlayer, 120u);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.objects_all_map")
			{
				result = buildAllPlayersObjectsSummaryCompact(25u, 40u);
				result["truncated"] = true;
				result["path"] = "game.objects_all_map";
				return true;
			}

			if (path == "game.objects_all")
			{
				result = buildAllPlayersObjectsSummary();
				return true;
			}

			if (path == "game.visible_enemies")
			{
				// Visibility is always from local player's perspective.
				result = buildVisibleEnemiesSummary(localPlayer);
				return true;
			}

			if (path == "game.player")
			{
				result = buildPlayerDetails(selectedPlayer);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			if (path == "game.players")
			{
				nlohmann::json players = nlohmann::json::array();
				const Int count = ThePlayerList->getPlayerCount();
				for (Int i = 0; i < count; ++i)
				{
					Player* player = ThePlayerList->getNthPlayer(i);
					if (player == nullptr)
					{
						continue;
					}

					nlohmann::json row = buildPlayerDetails(player);
					row["is_local_player"] = (player == localPlayer);
					players.push_back(row);
				}

				result = nlohmann::json{
					{"count", players.size()},
					{"players", players}
				};
				return true;
			}

			if (path == "game.status" || path == "game.summary" || path == "game.all")
			{
				result = buildPlayerDetails(selectedPlayer);
				result["is_local_player"] = (selectedPlayer == localPlayer);
				return true;
			}

			reason = "unsupported_query_path";
			return false;
		}

