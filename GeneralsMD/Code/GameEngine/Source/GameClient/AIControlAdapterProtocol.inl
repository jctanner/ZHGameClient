		static std::string getJsonString(const nlohmann::json& obj, const char* key)
		{
			const auto it = obj.find(key);
			if (it == obj.end() || !it->is_string())
			{
				return std::string();
			}
			return it->get<std::string>();
		}

		static std::string normalizeControlId(const std::string& controlId)
		{
			// Backward-compatible alias: "SoloPlay" maps to the real single-player button.
			if (controlId == "MainMenu.wnd:ButtonSoloPlay")
			{
				return "MainMenu.wnd:ButtonSinglePlayer";
			}
			// Convenience alias for LAN lobby back button.
			if (controlId == "ButtonBack")
			{
				return "LanLobbyMenu.wnd:ButtonBack";
			}
			if (controlId == "ButtonCreateGame")
			{
				return "LanLobbyMenu.wnd:ButtonHost";
			}
			if (controlId == "ButtonJoinGame")
			{
				return "LanLobbyMenu.wnd:ButtonJoin";
			}
			if (controlId == "ButtonDirectConnect")
			{
				return "LanLobbyMenu.wnd:ButtonDirectConnect";
			}
			return controlId;
		}

		GameWindow* findFirstUsableControl(const std::vector<std::string>& decoratedIds) const
		{
			if (TheNameKeyGenerator == nullptr || TheWindowManager == nullptr)
			{
				return nullptr;
			}

			for (const std::string& id : decoratedIds)
			{
				const NameKeyType key = TheNameKeyGenerator->nameToKey(id.c_str());
				GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);
				if (control == nullptr)
				{
					continue;
				}

				const UnsignedInt status = control->winGetStatus();
				if ((status & WIN_STATUS_HIDDEN) != 0u)
				{
					continue;
				}
				if ((status & WIN_STATUS_ENABLED) == 0u)
				{
					continue;
				}
				if (control->winGetParent() == nullptr)
				{
					continue;
				}

				return control;
			}

			return nullptr;
		}

		void sendJsonLine(const nlohmann::json& payload)
		{
			if (!m_hasClient)
			{
				return;
			}

			std::string line = payload.dump();
			adapterLog("send_raw %s", truncateForLog(line).c_str());
			line.push_back('\n');

			DWORD bytesWritten = 0;
			if (!::WriteFile(m_pipe, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr))
			{
				adapterLog("write_pipe_failed winerr=%lu", static_cast<unsigned long>(::GetLastError()));
				resetClientConnection();
			}
		}

		void sendProtocolError(const std::string& requestId, const char* code, const char* reason)
		{
			DEBUG_LOG(("[AICTRL] protocol_error request_id=%s code=%s reason=%s",
				requestId.c_str(),
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "Error"},
				{"request_id", requestId},
				{"code", code},
				{"reason", reason}
			};
			sendJsonLine(reply);
		}

		void sendActionAck(const std::string& requestId, bool ok, const char* code = nullptr, const char* reason = nullptr)
		{
			DEBUG_LOG(("[AICTRL] action_ack request_id=%s ok=%d code=%s reason=%s",
				requestId.c_str(),
				ok ? 1 : 0,
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "ActionAck"},
				{"request_id", requestId},
				{"ok", ok}
			};

			if (!ok)
			{
				reply["code"] = code != nullptr ? code : "internal_error";
				reply["reason"] = reason != nullptr ? reason : "request_failed";
			}

			sendJsonLine(reply);
		}

		void sendQueryResult(const std::string& requestId, const nlohmann::json& result)
		{
			DEBUG_LOG(("[AICTRL] query_result request_id=%s ok=1", requestId.c_str()));

			nlohmann::json reply = {
				{"type", "QueryResult"},
				{"request_id", requestId},
				{"ok", true},
				{"result", result}
			};
			sendJsonLine(reply);
		}

		void sendQueryError(const std::string& requestId, const char* code, const char* reason)
		{
			DEBUG_LOG(("[AICTRL] query_result request_id=%s ok=0 code=%s reason=%s",
				requestId.c_str(),
				code != nullptr ? code : "",
				reason != nullptr ? reason : ""));

			nlohmann::json reply = {
				{"type", "QueryResult"},
				{"request_id", requestId},
				{"ok", false},
				{"code", code != nullptr ? code : "invalid_state"},
				{"reason", reason != nullptr ? reason : "query_failed"}
			};
			sendJsonLine(reply);
		}

		void handleMessage(const std::string& line)
		{
			const nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
			if (message.is_discarded() || !message.is_object())
			{
				DEBUG_LOG(("[AICTRL] recv invalid_json"));
				sendProtocolError(std::string(), "bad_request", "invalid_json");
				return;
			}

			const std::string type = getJsonString(message, "type");
			const std::string requestId = getJsonString(message, "request_id");
			DEBUG_LOG(("[AICTRL] recv type=%s request_id=%s", type.c_str(), requestId.c_str()));

			if (type.empty())
			{
				sendProtocolError(requestId, "bad_request", "missing_type");
				return;
			}

			if (type == "Hello")
			{
				nlohmann::json reply = {
					{"type", "HelloAck"},
					{"request_id", requestId},
					{"ok", true},
					{"protocol", "zh-ai-control-v1"},
					{"adapter_version", "0.1.0"},
					{"session_id", m_sessionId},
					{"capabilities", nlohmann::json::array({
						"session",
						"menu_click",
						"menu_set_text",
						"chat_send",
						"game_query",
						"game_queue_unit",
						"game_queue_soldiers_all_barracks",
						"game_queue_rpg_all_barracks",
						"game_queue_quads_all_war_factories",
						"game_queue_scorpions_all_war_factories",
						"game_queue_radar_vans_all_war_factories",
						"game_queue_radar_van",
						"game_supply_build",
						"game_supply_build_smart",
						"game_barracks_build_smart",
						"game_arms_dealer_build_smart",
						"game_palace_build_smart",
						"game_black_market_build_smart",
						"game_attackmove_all_combat_to_player",
						"game_attackmove_raid_smart",
						"game_guard_all_idle_ground_combat",
						"adapter_log_configure",
						"adapter_log_reset",
						"game_camera_set",
						"game_camera_set_zoom_limited",
						"game_camera_lookat",
						"game_camera_get"
					})}
				};
				sendJsonLine(reply);
				return;
			}

			if (type == "Ping")
			{
				nlohmann::json reply = {
					{"type", "Pong"},
					{"request_id", requestId}
				};
				sendJsonLine(reply);
				return;
			}

			if (type != "SessionCommand")
			{
				sendProtocolError(requestId, "bad_request", "unsupported_type");
				return;
			}

			handleSessionCommand(message, requestId);
		}

		void handleSessionCommand(const nlohmann::json& message, const std::string& requestId)
		{
			const std::string cmd = getJsonString(message, "cmd");
			DEBUG_LOG(("[AICTRL] session_cmd request_id=%s cmd=%s", requestId.c_str(), cmd.c_str()));
			if (cmd.empty())
			{
				sendActionAck(requestId, false, "bad_request", "missing_cmd");
				return;
			}

			if (cmd == "Menu.Click")
			{
				std::string reason;
				if (!executeMenuClick(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Menu.SetText")
			{
				std::string reason;
				if (!executeMenuSetText(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Menu.ListControls")
			{
				sendQueryResult(requestId, buildControlsInventory(message));
				return;
			}

			if (cmd == "Chat.Send")
			{
				std::string reason;
				if (!executeChatSend(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.Query")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameQuery(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}

				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.Camera.Get")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameCameraGet(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}
				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.QueueUnit")
			{
				std::string reason;
				if (!executeGameQueueUnit(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.QueueSoldiersAllBarracks")
			{
				std::string reason;
				if (!executeGameQueueSoldiersAllBarracks(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.QueueRpgTroopersAllBarracks")
			{
				std::string reason;
				if (!executeGameQueueRpgTroopersAllBarracks(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.QueueQuadsAllWarFactories")
			{
				std::string reason;
				if (!executeGameQueueQuadsAllWarFactories(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.QueueScorpionsAllWarFactories")
			{
				std::string reason;
				if (!executeGameQueueScorpionsAllWarFactories(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.QueueRadarVansAllWarFactories")
			{
				std::string reason;
				if (!executeGameQueueRadarVansAllWarFactories(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.QueueRadarVan")
			{
				std::string reason;
				if (!executeGameQueueRadarVan(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildWorker")
			{
				std::string reason;
				if (!executeGameBuildWorker(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.FindSupplySources")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameFindSupplySources(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}
				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.FindBuildLocationNearSupply")
			{
				nlohmann::json result;
				std::string reason;
				if (!executeGameFindBuildLocationNearSupply(message, result, reason))
				{
					sendQueryError(requestId, "invalid_state", reason.c_str());
					return;
				}
				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Game.DozerConstruct")
			{
				std::string reason;
				if (!executeGameDozerConstruct(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildSupplyStashAuto")
			{
				std::string reason;
				if (!executeGameBuildSupplyStashAuto(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildSupplyStashSmart")
			{
				std::string reason;
				if (!executeGameBuildSupplyStashSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildBarracksSmart")
			{
				std::string reason;
				if (!executeGameBuildBarracksSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildCommandCenterSmart")
			{
				std::string reason;
				if (!executeGameBuildCommandCenterSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildArmsDealerSmart")
			{
				std::string reason;
				if (!executeGameBuildArmsDealerSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildPalaceSmart")
			{
				std::string reason;
				if (!executeGameBuildPalaceSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildBlackMarketSmart")
			{
				std::string reason;
				if (!executeGameBuildBlackMarketSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.AttackMove")
			{
				std::string reason;
				if (!executeGameAttackMove(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.AttackMoveAllCombatToPlayer")
			{
				std::string reason;
				if (!executeGameAttackMoveAllCombatToPlayer(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.AttackMove.RaidSmart")
			{
				std::string reason;
				if (!executeGameAttackMoveRaidSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.GuardAllIdleGroundCombat")
			{
				std::string reason;
				if (!executeGameGuardAllIdleGroundCombat(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Adapter.Log.Configure")
			{
				const auto argsIt = message.find("args");
				if (argsIt == message.end() || !argsIt->is_object())
				{
					sendActionAck(requestId, false, "bad_request", "missing_args");
					return;
				}

				const auto pathIt = argsIt->find("path");
				if (pathIt == argsIt->end() || !pathIt->is_string())
				{
					sendActionAck(requestId, false, "bad_request", "missing_log_path");
					return;
				}

				bool truncateExisting = false;
				const auto truncateIt = argsIt->find("truncate");
				if (truncateIt != argsIt->end() && truncateIt->is_boolean())
				{
					truncateExisting = truncateIt->get<bool>();
				}

				if (!configureAdapterLogPath(pathIt->get<std::string>(), truncateExisting))
				{
					sendActionAck(requestId, false, "invalid_state", "log_configure_failed");
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Adapter.Log.Reset")
			{
				bool truncateExisting = false;
				const auto argsIt = message.find("args");
				if (argsIt != message.end() && argsIt->is_object())
				{
					const auto truncateIt = argsIt->find("truncate");
					if (truncateIt != argsIt->end() && truncateIt->is_boolean())
					{
						truncateExisting = truncateIt->get<bool>();
					}
				}

				if (!resetAdapterLogFile(truncateExisting))
				{
					sendActionAck(requestId, false, "invalid_state", "log_reset_failed");
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.Camera.Set")
			{
				std::string reason;
				if (!executeGameCameraSet(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.Camera.SetZoomLimited")
			{
				std::string reason;
				if (!executeGameCameraSetZoomLimited(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.Camera.LookAt")
			{
				std::string reason;
				if (!executeGameCameraLookAt(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			sendActionAck(requestId, false, "unsupported_cmd", "unsupported_session_command");
		}
