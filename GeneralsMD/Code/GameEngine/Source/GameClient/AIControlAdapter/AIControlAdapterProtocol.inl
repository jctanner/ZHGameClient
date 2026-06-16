		/**
		 * AIControlAdapterProtocol.inl
		 *
		 * Named pipe protocol handling and message routing for AI Control Adapter.
		 *
		 * This file implements the communication protocol between external AI agents and the
		 * game. It defines:
		 * - Message format (JSON over named pipe)
		 * - Protocol handshake (Hello/HelloAck)
		 * - Command routing (type/cmd -> executor function)
		 * - Response formatting (success/error acknowledgements)
		 * - Capability negotiation (what commands are supported)
		 *
		 * PROTOCOL OVERVIEW
		 * =================
		 *
		 * Transport: Windows named pipe (\\.\pipe\zh_ai_control)
		 * - Line-delimited JSON messages (one JSON object per line)
		 * - Non-blocking I/O (game polls pipe each frame)
		 * - Client initiates connection, adapter accepts
		 * - Connection persists across game sessions
		 *
		 * Message Types:
		 * 1. Hello - Client handshake (get capabilities, session ID)
		 * 2. Ping - Keep-alive / latency check
		 * 3. SessionCommand - Execute a command (Menu, Game, Skirmish, etc.)
		 *
		 * SessionCommand Structure:
		 * {
		 *   "type": "SessionCommand",
		 *   "request_id": "unique-string",  // Client-provided, echoed in response
		 *   "cmd": "Category.Action",        // Command name (e.g., "Game.BuildWorker")
		 *   "args": { ... }                  // Command-specific arguments
		 * }
		 *
		 * Response Types:
		 * 1. HelloAck - Protocol info, capabilities, session ID
		 * 2. Pong - Ping response
		 * 3. ActionAck - Command execution result (ok: true/false, reason on failure)
		 * 4. QueryResult - Query response with data (ok: true, result: {...})
		 * 5. QueryError - Query failure (ok: false, error_type, reason)
		 * 6. ProtocolError - Protocol-level error (bad JSON, unknown command)
		 *
		 * Command Categories:
		 * - Menu.*: UI navigation (Menu.Click, Menu.SetText, Menu.SelectComboBox)
		 * - Chat.*: In-game chat (Chat.Send)
		 * - Game.*: Game actions (Game.BuildWorker, Game.QueueUnit, Game.Query)
		 * - Skirmish.*: Match setup (Skirmish.SetMap, Skirmish.Start)
		 * - Automation.*: Rule configuration (Automation.WorkerRule)
		 * - Autonomy.*: Autonomous mode control (Autonomy.Mode, Autonomy.Configure)
		 * - Adapter.*: Adapter configuration (Adapter.LogConfigure)
		 *
		 * Error Handling:
		 * - Protocol errors: Invalid JSON, missing fields, unknown command type
		 * - State errors: Command not valid in current game state (e.g., build in menu)
		 * - Execution errors: Command failed (e.g., not enough money, no producer)
		 *
		 * Capability Negotiation:
		 * - HelloAck returns "capabilities" array listing supported commands
		 * - Clients should check capabilities before sending commands
		 * - New capabilities can be added without breaking existing clients
		 *
		 * Threading Model:
		 * - All protocol handling runs on game's main thread
		 * - handleMessage() called from AIControlAdapterUpdate() each frame
		 * - Executor functions (executeMenuClick, executeGameQuery, etc.) are synchronous
		 * - No locks needed (single-threaded execution)
		 *
		 * See also:
		 * - scripts/configure-death-valley-final.ps1 for example PowerShell client
		 * - AIControlAdapterUI.inl for Menu.* command implementations
		 * - AIControlAdapterGameActions.inl for Game.* command implementations
		 * - AIControlAdapterSkirmish.inl for Skirmish.* command implementations
		 */

		// =============================================================================
		// JSON UTILITY FUNCTIONS
		// =============================================================================

		/**
		 * Safely extract a string field from JSON object.
		 *
		 * @param obj JSON object to read from
		 * @param key Field name to extract
		 * @return String value if field exists and is string type, empty string otherwise
		 */
		static std::string getJsonString(const nlohmann::json& obj, const char* key)
		{
			const auto it = obj.find(key);
			if (it == obj.end() || !it->is_string())
			{
				return std::string();
			}
			return it->get<std::string>();
		}

		/**
		 * Normalize control IDs to handle backward compatibility and convenience aliases.
		 *
		 * Control IDs in the game UI are fully-qualified names like "MainMenu.wnd:ButtonSinglePlayer".
		 * This function provides:
		 * - Backward compatibility: Old names map to current names
		 * - Convenience aliases: Short names for common controls
		 * - Consistent naming: Ensures clients use canonical control IDs
		 *
		 * Aliases:
		 * - "MainMenu.wnd:ButtonSoloPlay" -> "MainMenu.wnd:ButtonSinglePlayer" (renamed control)
		 * - "ButtonBack" -> "LanLobbyMenu.wnd:ButtonBack" (convenience short name)
		 * - "ButtonCreateGame" -> "LanLobbyMenu.wnd:ButtonHost" (convenience short name)
		 * - "ButtonJoinGame" -> "LanLobbyMenu.wnd:ButtonJoin" (convenience short name)
		 * - "ButtonDirectConnect" -> "LanLobbyMenu.wnd:ButtonDirectConnect" (convenience short name)
		 *
		 * @param controlId Raw control ID from client command
		 * @return Normalized control ID (canonical form)
		 */
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

		/**
		 * Find the first usable control from a list of control IDs.
		 *
		 * This function implements fallback logic for UI controls that may have different
		 * names or positions depending on the menu. For example, "Back" buttons have
		 * different control IDs in different menus:
		 * - MainMenu.wnd:ButtonSingleBack (single player menu)
		 * - MainMenu.wnd:ButtonMultiBack (multiplayer menu)
		 * - LanLobbyMenu.wnd:ButtonBack (LAN lobby)
		 *
		 * The function searches the list in order and returns the first control that:
		 * - Exists (found in window manager)
		 * - Is visible (not hidden)
		 * - Is enabled (clickable)
		 * - Has a parent (valid window hierarchy)
		 *
		 * This enables context-aware UI commands where the same logical action (e.g., "go back")
		 * works regardless of which menu the player is currently in.
		 *
		 * @param decoratedIds Vector of fully-qualified control IDs to try, in priority order
		 * @return First usable control found, or nullptr if none are usable
		 */
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

		// =============================================================================
		// MESSAGE HANDLING
		// =============================================================================

		/**
		 * Handle a single line of JSON input from the named pipe.
		 *
		 * This is the main protocol entry point, called from AIControlAdapterUpdate() each frame
		 * when data is available on the named pipe. It:
		 *
		 * 1. Parses JSON message
		 * 2. Validates message structure (must be object with "type" field)
		 * 3. Routes based on message type:
		 *    - "Hello": Protocol handshake, returns capabilities and session ID
		 *    - "Ping": Keep-alive, returns immediate Pong
		 *    - "SessionCommand": Execute a command, route to handleSessionCommand()
		 *
		 * Message Format:
		 * {
		 *   "type": "Hello" | "Ping" | "SessionCommand",
		 *   "request_id": "client-provided-string",  // Optional but recommended
		 *   ... type-specific fields ...
		 * }
		 *
		 * Error Handling:
		 * - Invalid JSON: sendProtocolError("invalid_json")
		 * - Missing type: sendProtocolError("missing_type")
		 * - Unknown type: sendProtocolError("unsupported_type")
		 *
		 * Hello Response Format:
		 * {
		 *   "type": "HelloAck",
		 *   "request_id": "<echoed from request>",
		 *   "ok": true,
		 *   "protocol": "zh-ai-control-v1",
		 *   "adapter_version": "0.1.0",
		 *   "session_id": "<unique session identifier>",
		 *   "capabilities": ["capability1", "capability2", ...]
		 * }
		 *
		 * The capabilities array lists all supported commands. Clients should check this
		 * before sending commands to ensure compatibility. Example capabilities:
		 * - "menu_click": Menu.Click command supported
		 * - "game_query": Game.Query command supported
		 * - "autonomy_mode": Autonomy.Mode command supported
		 *
		 * Ping/Pong:
		 * Clients can send Ping to check connectivity and measure latency. The adapter
		 * responds immediately with Pong (echoing request_id).
		 *
		 * @param line Raw JSON string from named pipe (line-delimited)
		 */
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
					{"session_id", m_log.sessionId()},
					{"capabilities", nlohmann::json::array({
						"session",
						"menu_click",
						"menu_set_text",
						"menu_select_combobox",
						"menu_set_slider",
						"chat_send",
						"game_query",
						"game_query_skirmish_setup",
						"game_queue_unit",
						"game_queue_upgrade",
						"game_purchase_science",
						"game_queue_soldiers_all_barracks",
						"game_queue_rpg_all_barracks",
						"game_queue_quads_all_war_factories",
						"game_queue_scorpions_all_war_factories",
						"game_queue_radar_vans_all_war_factories",
						"game_queue_radar_van",
						"game_supply_build",
						"game_supply_build_smart",
						"game_barracks_build_smart",
						"game_command_center_build_smart",
						"game_arms_dealer_build_smart",
						"game_palace_build_smart",
						"game_black_market_build_smart",
						"game_scud_storm_build_smart",
						"game_building_mix",
						"game_scud_storm_at_position",
						"game_scud_storm_at_player",
						"game_attackmove_all_combat_to_player",
						"game_attackmove_raid_smart",
						"game_guard_all_idle_ground_combat",
						"game_set_money_debug",
						"automation_worker_rule",
						"automation_stash_worker_rule",
						"automation_attack_rule",
						"automation_capture_rule",
						"automation_radar_van_rule",
						"autonomy_mode",
						"autonomy_configure",
						"autonomy_status",
						"autonomy_telemetry",
						"autonomy_zones_snapshot",
						"autonomy_pause",
						"autonomy_resume",
						"autonomy_reset",
						"adapter_log_configure",
						"adapter_log_reset",
						"game_camera_set",
						"game_camera_reset",
						"game_camera_set_zoom_limited",
						"game_camera_lookat",
						"game_camera_get",
						"skirmish_set_slot",
						"skirmish_set_map",
						"skirmish_set_starting_cash",
						"skirmish_set_superweapon_restriction",
						"skirmish_set_seed",
						"skirmish_start",
						"skirmish_configure",
						"skirmish_refresh_ui"
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

		/**
		 * Handle a SessionCommand message by routing to the appropriate executor function.
		 *
		 * SessionCommand is the workhorse message type - it executes actions in the game.
		 * This function:
		 * 1. Extracts "cmd" field (command name)
		 * 2. Routes to appropriate executor function based on cmd
		 * 3. Sends response (ActionAck or QueryResult)
		 *
		 * SessionCommand Format:
		 * {
		 *   "type": "SessionCommand",
		 *   "request_id": "client-string",
		 *   "cmd": "Category.Action",  // e.g., "Game.BuildWorker", "Menu.Click"
		 *   "args": { ... }             // Command-specific arguments
		 * }
		 *
		 * Command Categories:
		 *
		 * Menu Commands (UI navigation):
		 * - Menu.Click: Click a button (args: controlId)
		 * - Menu.SetText: Set text entry value (args: controlId, text)
		 * - Menu.SelectComboBox: Select combo box item (args: controlId, index)
		 * - Menu.SetSlider: Set slider value (args: controlId, value)
		 * - Menu.ListControls: Query available UI controls (args: kind, include_hidden)
		 *
		 * Chat Commands:
		 * - Chat.Send: Send in-game chat message (args: text, scope)
		 *
		 * Game Query Commands (read game state):
		 * - Game.Query: Get comprehensive game state snapshot (args: options)
		 * - Game.Camera.Get: Get current camera position/zoom (args: none)
		 *
		 * Game Action Commands (modify game state):
		 * - Game.QueueUnit: Queue unit production (args: template)
		 * - Game.QueueUpgrade: Purchase upgrade (args: upgrade)
		 * - Game.PurchaseScience: Research science (args: science)
		 * - Game.SetMoney: Set cash (debug) (args: money)
		 * - Game.DebugDeshroud: Remove fog of war (debug) (args: none)
		 * - Game.BuildWorker: Build worker/rebel (args: none)
		 * - Game.MoveArmy: Move combat units (args: position)
		 * - ... many more (see capabilities list)
		 *
		 * Skirmish Commands (match setup):
		 * - Skirmish.SetMap: Select map (args: map_name)
		 * - Skirmish.SetSlot: Configure player slot (args: slot, faction, difficulty)
		 * - Skirmish.Start: Start the match (args: none)
		 * - ... more setup commands
		 *
		 * Automation Commands (configure rules):
		 * - Automation.WorkerRule: Configure worker behavior (args: rule)
		 * - Automation.AttackRule: Configure attack behavior (args: rule)
		 * - ... more automation rules
		 *
		 * Autonomy Commands (autonomous mode control):
		 * - Autonomy.Mode: Enable/disable autonomy (args: enabled)
		 * - Autonomy.Configure: Set autonomy parameters (args: settings)
		 * - Autonomy.Status: Query autonomy state (args: none)
		 * - ... more autonomy controls
		 *
		 * Response Types:
		 * - ActionAck: Command execution result
		 *   {"type":"ActionAck", "request_id":"...", "ok":true|false, "reason":"..."}
		 * - QueryResult: Query response with data
		 *   {"type":"QueryResult", "request_id":"...", "ok":true, "result":{...}}
		 * - QueryError: Query failed
		 *   {"type":"QueryError", "request_id":"...", "ok":false, "error_type":"...", "reason":"..."}
		 *
		 * Error Reasons:
		 * - "missing_cmd": No "cmd" field in message
		 * - "bad_request": Invalid command syntax or missing required args
		 * - "invalid_state": Command not valid in current game state (e.g., build in menu)
		 * - "no_money": Not enough cash for purchase/build
		 * - "no_producer": Required building doesn't exist
		 * - "queue_full": Production queue is full
		 * - ... command-specific reasons
		 *
		 * @param message SessionCommand JSON object
		 * @param requestId Request ID from message (for response correlation)
		 */
		void handleSessionCommand(const nlohmann::json& message, const std::string& requestId)
		{
			const std::string cmd = getJsonString(message, "cmd");
			DEBUG_LOG(("[AICTRL] session_cmd request_id=%s cmd=%s", requestId.c_str(), cmd.c_str()));
			if (cmd.empty())
			{
				sendActionAck(requestId, false, "bad_request", "missing_cmd");
				return;
			}

			// =========================================================================
			// MENU COMMANDS
			// =========================================================================

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

			if (cmd == "Menu.SelectComboBox")
			{
				std::string reason;
				if (!executeMenuSelectComboBox(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Menu.GetListBoxContents")
			{
				std::string reason;
				nlohmann::json result;
				if (!executeMenuGetListBoxContents(message, result, reason))
				{
					sendQueryError(requestId, "query_failed", reason.c_str());
					return;
				}

				sendQueryResult(requestId, result);
				return;
			}

			if (cmd == "Menu.SelectListBox")
			{
				std::string reason;
				if (!executeMenuSelectListBox(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Menu.SetSlider")
			{
				std::string reason;
				if (!executeMenuSetSlider(message, reason))
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

			// =========================================================================
			// CHAT COMMANDS
			// =========================================================================

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

			// =========================================================================
			// GAME QUERY COMMANDS (read game state)
			// =========================================================================

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

			// =========================================================================
			// GAME ACTION COMMANDS (unit production, purchases, debug)
			// =========================================================================

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

			if (cmd == "Game.QueueUpgrade")
			{
				std::string reason;
				if (!executeGameQueueUpgrade(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.PurchaseScience")
			{
				std::string reason;
				if (!executeGamePurchaseScience(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.SetMoney")
			{
				std::string reason;
				if (!executeGameSetMoney(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}

				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.DebugDeshroud")
			{
				std::string reason;
				if (!executeGameDebugDeshroud(message, reason))
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

			if (cmd == "Game.QueueRocketBuggiesAllWarFactories")
			{
				std::string reason;
				if (!executeGameQueueRocketBuggiesAllWarFactories(message, reason))
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

			// =========================================================================
			// AUTOMATION COMMANDS (configure automation rules)
			// =========================================================================

			if (cmd == "Automation.ConfigureWorkerRule")
			{
				std::string reason;
				if (!configureWorkerAutomationRule(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ClearWorkerRule")
			{
				clearWorkerAutomationRule();
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ConfigureStashWorkerRule")
			{
				std::string reason;
				if (!configureStashWorkerAutomationRule(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ClearStashWorkerRule")
			{
				clearStashWorkerAutomationRule();
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ConfigureAttackRule")
			{
				std::string reason;
				if (!configureAttackAutomationRule(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ClearAttackRule")
			{
				clearAttackAutomationRule();
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ConfigureCaptureRule")
			{
				std::string reason;
				if (!configureCaptureAutomationRule(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ClearCaptureRule")
			{
				clearCaptureAutomationRule();
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ConfigureRadarVanRule")
			{
				std::string reason;
				if (!configureRadarVanAutomationRule(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Automation.ClearRadarVanRule")
			{
				clearRadarVanAutomationRule();
				sendActionAck(requestId, true);
				return;
			}

			// =========================================================================
			// AUTONOMY COMMANDS (autonomous mode control)
			// =========================================================================

			if (cmd == "Autonomy.SetMode")
			{
				std::string reason;
				if (!setAutonomyMode(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Autonomy.Configure")
			{
				std::string reason;
				if (!configureAutonomy(message, reason))
				{
					sendActionAck(requestId, false, "bad_request", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Autonomy.Status")
			{
				sendQueryResult(requestId, buildAutonomyStatus());
				return;
			}

			if (cmd == "Autonomy.Telemetry")
			{
				sendQueryResult(requestId, buildAutonomyTelemetry());
				return;
			}

			if (cmd == "Autonomy.ZonesSnapshot")
			{
				sendQueryResult(requestId, buildAutonomyZonesSnapshot());
				return;
			}

			if (cmd == "Autonomy.Pause")
			{
				pauseAutonomy();
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Autonomy.Resume")
			{
				resumeAutonomy();
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Autonomy.Reset")
			{
				resetAutonomy();
				sendActionAck(requestId, true);
				return;
			}

			// =========================================================================
			// GAME BUILDING COMMANDS (smart construction)
			// =========================================================================

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

			if (cmd == "Game.BuildScudStormSmart")
			{
				std::string reason;
				if (!executeGameBuildScudStormSmart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.BuildBuildingMix")
			{
				std::string reason;
				if (!executeGameBuildBuildingMix(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			// =========================================================================
			// GAME SPECIAL POWER COMMANDS (superweapons, abilities)
			// =========================================================================

			if (cmd == "Game.ScudStormAtPosition")
			{
				std::string reason;
				if (!executeGameScudStormAtPosition(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.ScudStormAtPlayer")
			{
				std::string reason;
				if (!executeGameScudStormAtPlayer(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			// =========================================================================
			// GAME COMBAT COMMANDS (army movement, attacks)
			// =========================================================================

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

			if (cmd == "Game.Move")
			{
				std::string reason;
				if (!executeGameMove(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Game.CaptureBuilding")
			{
				std::string reason;
				if (!executeGameCaptureBuilding(message, reason))
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

			// =========================================================================
			// ADAPTER COMMANDS (adapter configuration)
			// =========================================================================

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

				if (!m_log.configurePath(pathIt->get<std::string>(), truncateExisting))
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

				if (!m_log.resetFile(truncateExisting))
				{
					sendActionAck(requestId, false, "invalid_state", "log_reset_failed");
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			// =========================================================================
			// CAMERA COMMANDS (camera control)
			// =========================================================================

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

			if (cmd == "Game.Camera.Reset")
			{
				std::string reason;
				if (!executeGameCameraReset(reason))
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

			// =========================================================================
			// SKIRMISH COMMANDS (match setup)
			// =========================================================================

			if (cmd == "Skirmish.SetSlot")
			{
				std::string reason;
				if (!executeSkirmishSetSlot(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.SetMap")
			{
				std::string reason;
				if (!executeSkirmishSetMap(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.SetStartingCash")
			{
				std::string reason;
				if (!executeSkirmishSetStartingCash(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.SetSuperweaponRestriction")
			{
				std::string reason;
				if (!executeSkirmishSetSuperweaponRestriction(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.SetSeed")
			{
				std::string reason;
				if (!executeSkirmishSetSeed(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.Start")
			{
				std::string reason;
				if (!executeSkirmishStart(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.Configure")
			{
				std::string reason;
				if (!executeSkirmishConfigure(message, reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			if (cmd == "Skirmish.RefreshUI")
			{
				std::string reason;
				if (!executeSkirmishRefreshUI(reason))
				{
					sendActionAck(requestId, false, "invalid_state", reason.c_str());
					return;
				}
				sendActionAck(requestId, true);
				return;
			}

			sendActionAck(requestId, false, "unsupported_cmd", "unsupported_session_command");
		}
