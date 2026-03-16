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
			nlohmann::json units = nlohmann::json::array();
			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"units", units},
					{"units_total", 0}
				};
			}

			struct UnitsCollectContext
			{
				AIControlAdapterState* self;
				nlohmann::json* units;
				std::size_t maxUnits;
				Int totalUnits;
			};

			UnitsCollectContext ctx = { this, &units, maxUnits, 0 };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					return;
				}

				UnitsCollectContext* ctx = static_cast<UnitsCollectContext*>(userData);
				++ctx->totalUnits;
				if (ctx->units->size() < ctx->maxUnits)
				{
					ctx->units->push_back(ctx->self->buildObjectMapRow(obj, true));
				}
			}, &ctx);

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"units", units},
				{"units_total", ctx.totalUnits},
				{"truncated", units.size() < static_cast<std::size_t>(ctx.totalUnits)},
				{"path", "game.objects_units_map"}
			};
		}

		nlohmann::json buildOwnedObjectsBuildingsMap(Player* player, std::size_t maxBuildings)
		{
			nlohmann::json buildings = nlohmann::json::array();
			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"buildings", buildings},
					{"buildings_total", 0}
				};
			}

			struct BuildingsCollectContext
			{
				AIControlAdapterState* self;
				nlohmann::json* buildings;
				std::size_t maxBuildings;
				Int totalBuildings;
			};

			BuildingsCollectContext ctx = { this, &buildings, maxBuildings, 0 };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				if (!obj->isKindOf(KINDOF_STRUCTURE))
				{
					return;
				}

				BuildingsCollectContext* ctx = static_cast<BuildingsCollectContext*>(userData);
				++ctx->totalBuildings;
				if (ctx->buildings->size() < ctx->maxBuildings)
				{
					ctx->buildings->push_back(ctx->self->buildObjectMapRow(obj, true));
				}
			}, &ctx);

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"buildings", buildings},
				{"buildings_total", ctx.totalBuildings},
				{"truncated", buildings.size() < static_cast<std::size_t>(ctx.totalBuildings)},
				{"path", "game.objects_buildings_map"}
			};
		}

		nlohmann::json buildIdleWorkersSummary(Player* player, std::size_t maxWorkers)
		{
			nlohmann::json workers = nlohmann::json::array();
			if (player == nullptr)
			{
				return nlohmann::json{
					{"player_index", -1},
					{"workers", workers},
					{"idle_workers_total", 0}
				};
			}

			struct IdleWorkerCollectContext
			{
				AIControlAdapterState* self;
				nlohmann::json* workers;
				std::size_t maxWorkers;
				Int totalIdleWorkers;
			};

			IdleWorkerCollectContext ctx = { this, &workers, maxWorkers, 0 };
			player->iterateObjects([](Object* obj, void* userData)
			{
				if (obj == nullptr || userData == nullptr || obj->isEffectivelyDead())
				{
					return;
				}
				if (obj->isKindOf(KINDOF_STRUCTURE))
				{
					return;
				}
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

				IdleWorkerCollectContext* ctx = static_cast<IdleWorkerCollectContext*>(userData);
				++ctx->totalIdleWorkers;
				if (ctx->workers->size() < ctx->maxWorkers)
				{
					ctx->workers->push_back(ctx->self->buildObjectMapRow(obj, true));
				}
			}, &ctx);

			return nlohmann::json{
				{"player_index", player->getPlayerIndex()},
				{"workers", workers},
				{"idle_workers_total", ctx.totalIdleWorkers},
				{"truncated", workers.size() < static_cast<std::size_t>(ctx.totalIdleWorkers)},
				{"path", "game.idle_workers"}
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

