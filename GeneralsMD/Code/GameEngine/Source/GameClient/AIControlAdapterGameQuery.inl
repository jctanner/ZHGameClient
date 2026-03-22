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

		nlohmann::json buildUnitCompositionSummary(Player* player) const
		{
			nlohmann::json counts = nlohmann::json::object({
				{"workers", 0},
				{"rebels", 0},
				{"rpg", 0},
				{"radar_vans", 0},
				{"quads", 0},
				{"scorpions", 0},
				{"combat_units", 0},
				{"ground_combat_units", 0},
				{"units_total", 0}
			});

			if (player == nullptr)
			{
				return counts;
			}

			auto toLower = [](std::string value) -> std::string
			{
				std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) -> unsigned char
				{
					return static_cast<unsigned char>(std::tolower(ch));
				});
				return value;
			};

			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead() || obj->isKindOf(KINDOF_STRUCTURE))
				{
					return;
				}

				nlohmann::json* counts = static_cast<nlohmann::json*>(userData);
				(*counts)["units_total"] = counts->value("units_total", 0) + 1;

				const bool isDozer = obj->isKindOf(KINDOF_DOZER);
				const bool isHarvester = obj->isKindOf(KINDOF_HARVESTER);
				const bool isAircraft = obj->isKindOf(KINDOF_AIRCRAFT);
				const bool isGroundCombat =
					!isDozer &&
					!isHarvester &&
					!isAircraft &&
					(obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_VEHICLE)) &&
					obj->isAbleToAttack();
				if (isGroundCombat)
				{
					(*counts)["ground_combat_units"] = counts->value("ground_combat_units", 0) + 1;
					(*counts)["combat_units"] = counts->value("combat_units", 0) + 1;
				}
				else if (!isDozer && !isHarvester && obj->isAbleToAttack())
				{
					(*counts)["combat_units"] = counts->value("combat_units", 0) + 1;
				}

				const ThingTemplate* tmpl = obj->getTemplate();
				const std::string name = tmpl != nullptr ? tmpl->getName().str() : "";
				std::string text = name;
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) -> unsigned char
				{
					return static_cast<unsigned char>(std::tolower(ch));
				});

				auto contains = [&text](const char* needle) -> bool
				{
					return text.find(needle) != std::string::npos;
				};

				if (contains("worker") || contains("dozer"))
				{
					(*counts)["workers"] = counts->value("workers", 0) + 1;
				}
				if (contains("rebel"))
				{
					(*counts)["rebels"] = counts->value("rebels", 0) + 1;
				}
				if (contains("rpg") || contains("tunneldefender"))
				{
					(*counts)["rpg"] = counts->value("rpg", 0) + 1;
				}
				if (contains("radarvan") || contains("radar_van"))
				{
					(*counts)["radar_vans"] = counts->value("radar_vans", 0) + 1;
				}
				if (contains("quad"))
				{
					(*counts)["quads"] = counts->value("quads", 0) + 1;
				}
				if (contains("scorpion"))
				{
					(*counts)["scorpions"] = counts->value("scorpions", 0) + 1;
				}
			}, &counts);

			return counts;
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

		nlohmann::json buildCapturableBuildingsSummary(Player* localPlayer)
		{
			nlohmann::json buildings = nlohmann::json::array();
			if (ThePlayerList == nullptr || localPlayer == nullptr)
			{
				return nlohmann::json{
					{"count", 0},
					{"buildings", buildings}
				};
			}

			const Int count = ThePlayerList->getPlayerCount();
			for (Int i = 0; i < count; ++i)
			{
				Player* player = ThePlayerList->getNthPlayer(i);
				if (player == nullptr)
				{
					continue;
				}

				struct CapturableCollectContext
				{
					AIControlAdapterState* self;
					Player* localPlayer;
					Player* ownerPlayer;
					nlohmann::json* buildings;
				} ctx = { this, localPlayer, player, &buildings };

				player->iterateObjects(
					[](Object* obj, void* userData)
					{
						if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
						{
							return;
						}
						if (!obj->isKindOf(KINDOF_STRUCTURE))
						{
							return;
						}
						if (!obj->isKindOf(KINDOF_CAPTURABLE) && !obj->isKindOf(KINDOF_TECH_BUILDING) && !obj->isKindOf(KINDOF_TECH_BASE_DEFENSE))
						{
							return;
						}
						if (obj->isKindOf(KINDOF_IMMUNE_TO_CAPTURE))
						{
							return;
						}
						if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD))
						{
							return;
						}

						CapturableCollectContext* ctx = static_cast<CapturableCollectContext*>(userData);
						const Player* ownerPlayer = obj->getControllingPlayer();
						if (ownerPlayer != nullptr)
						{
							const Relationship relationship = ctx->localPlayer->getRelationship(ownerPlayer->getDefaultTeam());
							if (relationship == ALLIES)
							{
								return;
							}
						}

						nlohmann::json row = ctx->self->buildObjectSummaryRow(obj, false);
						row["player_index"] = ownerPlayer != nullptr ? ownerPlayer->getPlayerIndex() : -1;
						row["capturable"] = true;
						ctx->buildings->push_back(row);
					},
					&ctx
				);
			}

			return nlohmann::json{
				{"count", buildings.size()},
				{"buildings", buildings},
				{"path", "game.capturable_buildings"}
			};
		}

		struct GridConfig
		{
			Int cols;
			Int rows;
			Real minX;
			Real minY;
			Real maxX;
			Real maxY;
			Real cellWidth;
			Real cellHeight;
		};

		static Int clampGridDimension(Int value)
		{
			if (value < 1)
			{
				return 1;
			}
			if (value > 128)
			{
				return 128;
			}
			return value;
		}

		static std::string makeGridColumnLabel(Int colIndex)
		{
			if (colIndex < 0)
			{
				return std::string();
			}

			std::string out;
			Int n = colIndex;
			do
			{
				const Int rem = n % 26;
				out.insert(out.begin(), static_cast<char>('A' + rem));
				n = (n / 26) - 1;
			} while (n >= 0);
			return out;
		}

		static std::string makeGridCellLabel(Int colIndex, Int rowIndex)
		{
			if (colIndex < 0 || rowIndex < 0)
			{
				return std::string();
			}
			char buffer[32];
			sprintf_s(buffer, "%s%d", makeGridColumnLabel(colIndex).c_str(), rowIndex + 1);
			return std::string(buffer);
		}

		static bool parseGridCellLabel(const std::string& rawLabel, Int& outColIndex, Int& outRowIndex)
		{
			outColIndex = -1;
			outRowIndex = -1;

			std::string label;
			label.reserve(rawLabel.size());
			for (std::size_t i = 0; i < rawLabel.size(); ++i)
			{
				const char ch = rawLabel[i];
				if (!std::isspace(static_cast<unsigned char>(ch)))
				{
					label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
				}
			}
			if (label.empty())
			{
				return false;
			}

			std::size_t split = 0;
			while (split < label.size() && std::isalpha(static_cast<unsigned char>(label[split])))
			{
				++split;
			}
			if (split == 0 || split >= label.size())
			{
				return false;
			}

			Int colIndex = 0;
			for (std::size_t i = 0; i < split; ++i)
			{
				colIndex = (colIndex * 26) + (label[i] - 'A' + 1);
			}
			colIndex -= 1;

			Int rowNumber = 0;
			for (std::size_t i = split; i < label.size(); ++i)
			{
				if (!std::isdigit(static_cast<unsigned char>(label[i])))
				{
					return false;
				}
				rowNumber = (rowNumber * 10) + (label[i] - '0');
			}
			if (rowNumber <= 0)
			{
				return false;
			}

			outColIndex = colIndex;
			outRowIndex = rowNumber - 1;
			return true;
		}

		GridConfig resolveGridConfig(const nlohmann::json* args) const
		{
			GridConfig cfg = {};
			cfg.cols = 32;
			cfg.rows = 32;
			cfg.minX = 0.0f;
			cfg.minY = 0.0f;
			cfg.maxX = 1024.0f;
			cfg.maxY = 1024.0f;

			if (args != nullptr && args->is_object())
			{
				const auto colsIt = args->find("grid_cols");
				if (colsIt != args->end() && colsIt->is_number_integer())
				{
					cfg.cols = clampGridDimension(colsIt->get<Int>());
				}
				const auto rowsIt = args->find("grid_rows");
				if (rowsIt != args->end() && rowsIt->is_number_integer())
				{
					cfg.rows = clampGridDimension(rowsIt->get<Int>());
				}
			}

			if (TheTerrainLogic != nullptr)
			{
				Region3D extent = {};
				TheTerrainLogic->getExtent(&extent);
				cfg.minX = extent.lo.x;
				cfg.minY = extent.lo.y;
				cfg.maxX = extent.hi.x;
				cfg.maxY = extent.hi.y;
			}

			if (!(cfg.maxX > cfg.minX))
			{
				cfg.maxX = cfg.minX + 1.0f;
			}
			if (!(cfg.maxY > cfg.minY))
			{
				cfg.maxY = cfg.minY + 1.0f;
			}

			cfg.cellWidth = (cfg.maxX - cfg.minX) / static_cast<Real>(cfg.cols);
			cfg.cellHeight = (cfg.maxY - cfg.minY) / static_cast<Real>(cfg.rows);
			if (!(cfg.cellWidth > 0.0f))
			{
				cfg.cellWidth = 1.0f;
			}
			if (!(cfg.cellHeight > 0.0f))
			{
				cfg.cellHeight = 1.0f;
			}
			return cfg;
		}

		bool tryGetGridCellForPosition(const GridConfig& cfg, const Coord3D* pos, Int& outCol, Int& outRow) const
		{
			outCol = -1;
			outRow = -1;
			if (pos == nullptr)
			{
				return false;
			}

			const Real clampedX = std::max(cfg.minX, std::min(pos->x, cfg.maxX - 0.001f));
			const Real clampedY = std::max(cfg.minY, std::min(pos->y, cfg.maxY - 0.001f));
			const Real normX = (clampedX - cfg.minX) / std::max(cfg.maxX - cfg.minX, 1.0f);
			const Real normY = (clampedY - cfg.minY) / std::max(cfg.maxY - cfg.minY, 1.0f);
			outCol = std::min<Int>(cfg.cols - 1, std::max<Int>(0, static_cast<Int>(normX * cfg.cols)));
			outRow = std::min<Int>(cfg.rows - 1, std::max<Int>(0, static_cast<Int>(normY * cfg.rows)));
			return true;
		}

		nlohmann::json buildGridCellBoundsJson(const GridConfig& cfg, Int col, Int row) const
		{
			const Real minX = cfg.minX + (cfg.cellWidth * static_cast<Real>(col));
			const Real minY = cfg.minY + (cfg.cellHeight * static_cast<Real>(row));
			const Real maxX = minX + cfg.cellWidth;
			const Real maxY = minY + cfg.cellHeight;
			return nlohmann::json::object({
				{"min_x", minX},
				{"min_y", minY},
				{"max_x", maxX},
				{"max_y", maxY},
				{"center_x", minX + (cfg.cellWidth * 0.5f)},
				{"center_y", minY + (cfg.cellHeight * 0.5f)}
			});
		}

		struct GridCellAggregate
		{
			Int col;
			Int row;
			Int units;
			Int buildings;
			std::map<Int, Int> totalsByPlayer;
			std::map<Int, Int> unitsByPlayer;
			std::map<Int, Int> buildingsByPlayer;
			GridCellAggregate() : col(0), row(0), units(0), buildings(0) {}
		};

		struct GridSummaryCollectContext
		{
			const AIControlAdapterState* self;
			const GridConfig* cfg;
			Int playerIndex;
			std::map<std::pair<Int, Int>, GridCellAggregate>* cells;
		};

		static void collectGridSummaryObjectCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}

			GridSummaryCollectContext* ctx = static_cast<GridSummaryCollectContext*>(userData);
			Int col = -1;
			Int row = -1;
			if (!ctx->self->tryGetGridCellForPosition(*ctx->cfg, obj->getPosition(), col, row))
			{
				return;
			}

			const std::pair<Int, Int> key(col, row);
			GridCellAggregate& cell = (*ctx->cells)[key];
			cell.col = col;
			cell.row = row;
			cell.totalsByPlayer[ctx->playerIndex] += 1;
			if (obj->isKindOf(KINDOF_STRUCTURE))
			{
				cell.buildings += 1;
				cell.buildingsByPlayer[ctx->playerIndex] += 1;
			}
			else
			{
				cell.units += 1;
				cell.unitsByPlayer[ctx->playerIndex] += 1;
			}
		}

		struct GridObjectsCollectContext
		{
			const AIControlAdapterState* self;
			const GridConfig* cfg;
			Int playerIndex;
			Int targetCol;
			Int targetRow;
			std::size_t maxObjects;
			nlohmann::json* objects;
			Int* totalObjects;
			Int* units;
			Int* buildings;
		};

		static void collectGridObjectsCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
			{
				return;
			}

			GridObjectsCollectContext* ctx = static_cast<GridObjectsCollectContext*>(userData);
			Int col = -1;
			Int row = -1;
			if (!ctx->self->tryGetGridCellForPosition(*ctx->cfg, obj->getPosition(), col, row))
			{
				return;
			}
			if (col != ctx->targetCol || row != ctx->targetRow)
			{
				return;
			}

			++(*ctx->totalObjects);
			if (obj->isKindOf(KINDOF_STRUCTURE))
			{
				++(*ctx->buildings);
			}
			else
			{
				++(*ctx->units);
			}

			if (ctx->objects->size() >= ctx->maxObjects)
			{
				return;
			}

			nlohmann::json rowJson = ctx->self->buildObjectSummaryRow(obj, true);
			rowJson["player_index"] = ctx->playerIndex;
			rowJson["cell"] = makeGridCellLabel(ctx->targetCol, ctx->targetRow);
			ctx->objects->push_back(rowJson);
		}

		nlohmann::json buildGridSummary(const GridConfig& cfg) const
		{
			std::map<std::pair<Int, Int>, GridCellAggregate> cells;
			if (ThePlayerList != nullptr)
			{
				const Player* neutralPlayer = ThePlayerList->getNeutralPlayer();
				const Int count = ThePlayerList->getPlayerCount();
				for (Int i = 0; i < count; ++i)
				{
					Player* player = ThePlayerList->getNthPlayer(i);
					if (player == nullptr || player == neutralPlayer || isCivilianLikePlayer(player))
					{
						continue;
					}

					GridSummaryCollectContext ctx = { this, &cfg, player->getPlayerIndex(), &cells };
					player->iterateObjects(collectGridSummaryObjectCallback, &ctx);
				}
			}

			nlohmann::json rows = nlohmann::json::array();
			for (std::map<std::pair<Int, Int>, GridCellAggregate>::const_iterator it = cells.begin(); it != cells.end(); ++it)
			{
				const GridCellAggregate& cell = it->second;
				Int dominantPlayerIndex = -1;
				Int dominantCount = 0;
				nlohmann::json playerCounts = nlohmann::json::object();
				for (std::map<Int, Int>::const_iterator pit = cell.totalsByPlayer.begin(); pit != cell.totalsByPlayer.end(); ++pit)
				{
					const Int playerIndex = pit->first;
					const Int total = pit->second;
					if (total > dominantCount)
					{
						dominantCount = total;
						dominantPlayerIndex = playerIndex;
					}

					nlohmann::json p = nlohmann::json::object();
					p["total"] = total;
					p["units"] = 0;
					p["buildings"] = 0;
					const std::map<Int, Int>::const_iterator unitIt = cell.unitsByPlayer.find(playerIndex);
					if (unitIt != cell.unitsByPlayer.end())
					{
						p["units"] = unitIt->second;
					}
					const std::map<Int, Int>::const_iterator buildingIt = cell.buildingsByPlayer.find(playerIndex);
					if (buildingIt != cell.buildingsByPlayer.end())
					{
						p["buildings"] = buildingIt->second;
					}
					playerCounts[std::to_string(playerIndex)] = p;
				}

				nlohmann::json row = nlohmann::json::object({
					{"cell", makeGridCellLabel(cell.col, cell.row)},
					{"col", cell.col},
					{"row", cell.row},
					{"units", cell.units},
					{"buildings", cell.buildings},
					{"objects_total", cell.units + cell.buildings},
					{"dominant_player_index", dominantPlayerIndex},
					{"player_counts", playerCounts}
				});
				row["bounds"] = buildGridCellBoundsJson(cfg, cell.col, cell.row);
				rows.push_back(row);
			}

			return nlohmann::json::object({
				{"path", "game.grid"},
				{"grid_cols", cfg.cols},
				{"grid_rows", cfg.rows},
				{"map", nlohmann::json::object({
					{"width", cfg.maxX - cfg.minX},
					{"height", cfg.maxY - cfg.minY},
					{"min_x", cfg.minX},
					{"min_y", cfg.minY},
					{"max_x", cfg.maxX},
					{"max_y", cfg.maxY}
				})},
				{"cells", rows},
				{"occupied_cell_count", rows.size()}
			});
		}

		nlohmann::json buildGridObjectsSummary(const GridConfig& cfg, const std::vector<std::pair<Int, Int>>& requestedCells, std::size_t maxObjectsPerCell) const
		{
			nlohmann::json cells = nlohmann::json::array();
			std::set<std::pair<Int, Int>> requestedSet(requestedCells.begin(), requestedCells.end());

			for (std::vector<std::pair<Int, Int>>::const_iterator reqIt = requestedCells.begin(); reqIt != requestedCells.end(); ++reqIt)
			{
				const Int col = reqIt->first;
				const Int row = reqIt->second;
				nlohmann::json objects = nlohmann::json::array();
				Int totalObjects = 0;
				Int units = 0;
				Int buildings = 0;

				if (ThePlayerList != nullptr)
				{
					const Player* neutralPlayer = ThePlayerList->getNeutralPlayer();
					const Int count = ThePlayerList->getPlayerCount();
					for (Int i = 0; i < count; ++i)
					{
						Player* player = ThePlayerList->getNthPlayer(i);
						if (player == nullptr || player == neutralPlayer || isCivilianLikePlayer(player))
						{
							continue;
						}

						GridObjectsCollectContext ctx = {
							this,
							&cfg,
							player->getPlayerIndex(),
							col,
							row,
							maxObjectsPerCell,
							&objects,
							&totalObjects,
							&units,
							&buildings
						};
						player->iterateObjects(collectGridObjectsCallback, &ctx);
					}
				}

				nlohmann::json cellJson = nlohmann::json::object({
					{"cell", makeGridCellLabel(col, row)},
					{"col", col},
					{"row", row},
					{"units_total", units},
					{"buildings_total", buildings},
					{"objects_total", totalObjects},
					{"truncated", objects.size() < static_cast<std::size_t>(totalObjects)},
					{"objects", objects}
				});
				cellJson["bounds"] = buildGridCellBoundsJson(cfg, col, row);
				cells.push_back(cellJson);
			}

			return nlohmann::json::object({
				{"path", "game.grid_objects"},
				{"grid_cols", cfg.cols},
				{"grid_rows", cfg.rows},
				{"cells", cells},
				{"count", cells.size()}
			});
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

			if (path == "game.unit_composition")
			{
				result = buildUnitCompositionSummary(selectedPlayer);
				result["player_index"] = selectedPlayer != nullptr ? selectedPlayer->getPlayerIndex() : -1;
				result["path"] = "game.unit_composition";
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

			if (path == "game.capturable_buildings")
			{
				result = buildCapturableBuildingsSummary(localPlayer);
				return true;
			}

			if (path == "game.grid")
			{
				const GridConfig cfg = resolveGridConfig(argsIt != message.end() && argsIt->is_object() ? &(*argsIt) : nullptr);
				result = buildGridSummary(cfg);
				return true;
			}

			if (path == "game.grid_objects")
			{
				const nlohmann::json* argsObj = (argsIt != message.end() && argsIt->is_object()) ? &(*argsIt) : nullptr;
				const GridConfig cfg = resolveGridConfig(argsObj);
				std::vector<std::pair<Int, Int>> requestedCells;

				if (argsObj != nullptr)
				{
					const auto cellIt = argsObj->find("cell");
					if (cellIt != argsObj->end() && cellIt->is_string())
					{
						Int col = -1;
						Int row = -1;
						if (!parseGridCellLabel(cellIt->get<std::string>(), col, row))
						{
							reason = "invalid_grid_cell";
							return false;
						}
						if (col < 0 || row < 0 || col >= cfg.cols || row >= cfg.rows)
						{
							reason = "grid_cell_out_of_bounds";
							return false;
						}
						requestedCells.push_back(std::make_pair(col, row));
					}

					const auto cellsIt = argsObj->find("cells");
					if (cellsIt != argsObj->end() && cellsIt->is_array())
					{
						for (nlohmann::json::const_iterator it = cellsIt->begin(); it != cellsIt->end(); ++it)
						{
							if (!it->is_string())
							{
								continue;
							}
							Int col = -1;
							Int row = -1;
							if (!parseGridCellLabel(it->get<std::string>(), col, row))
							{
								continue;
							}
							if (col < 0 || row < 0 || col >= cfg.cols || row >= cfg.rows)
							{
								continue;
							}
							requestedCells.push_back(std::make_pair(col, row));
						}
					}
				}

				if (requestedCells.empty())
				{
					reason = "missing_grid_cell";
					return false;
				}

				std::sort(requestedCells.begin(), requestedCells.end());
				requestedCells.erase(std::unique(requestedCells.begin(), requestedCells.end()), requestedCells.end());

				std::size_t maxObjectsPerCell = 120u;
				if (argsObj != nullptr)
				{
					const auto maxIt = argsObj->find("max_objects_per_cell");
					if (maxIt != argsObj->end() && maxIt->is_number_integer())
					{
						const Int parsed = maxIt->get<Int>();
						if (parsed > 0)
						{
							maxObjectsPerCell = static_cast<std::size_t>(std::min<Int>(parsed, 500));
						}
					}
				}

				result = buildGridObjectsSummary(cfg, requestedCells, maxObjectsPerCell);
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

