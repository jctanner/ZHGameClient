		static const char* classifyControlType(UnsignedInt style)
		{
			if ((style & GWS_PUSH_BUTTON) != 0u)
			{
				return "button";
			}
			if ((style & GWS_ENTRY_FIELD) != 0u)
			{
				return "text_entry";
			}
			if ((style & GWS_COMBO_BOX) != 0u)
			{
				return "combo_box";
			}
			if ((style & GWS_SCROLL_LISTBOX) != 0u)
			{
				return "list_box";
			}
			if ((style & GWS_CHECK_BOX) != 0u)
			{
				return "check_box";
			}
			if ((style & GWS_RADIO_BUTTON) != 0u)
			{
				return "radio_button";
			}
			return "other";
		}

		static bool includeType(const std::string& requestedKind, const char* actualKind)
		{
			if (requestedKind == "all")
			{
				return true;
			}
			if (requestedKind == "button")
			{
				return strcmp(actualKind, "button") == 0;
			}
			if (requestedKind == "text_entry")
			{
				return strcmp(actualKind, "text_entry") == 0;
			}
			return true;
		}

		void collectControlsRecursive(GameWindow* window, bool includeHidden, const std::string& kind, nlohmann::json& controls)
		{
			for (GameWindow* current = window; current != nullptr; current = current->winGetNext())
			{
				WinInstanceData* inst = current->winGetInstanceData();
				if (inst != nullptr)
				{
					const UnsignedInt status = current->winGetStatus();
					const bool isHidden = (status & WIN_STATUS_HIDDEN) != 0u;
					const bool isEnabled = (status & WIN_STATUS_ENABLED) != 0u;
					const UnsignedInt style = current->winGetStyle();
					const char* controlType = classifyControlType(style);

					if ((style & GWS_GADGET_WINDOW) != 0u && (!isHidden || includeHidden) && includeType(kind, controlType))
					{
						nlohmann::json control = {
							{"controlId", inst->m_decoratedNameString.str()},
							{"windowId", current->winGetWindowId()},
							{"type", controlType},
							{"hidden", isHidden},
							{"enabled", isEnabled}
						};
						controls.push_back(control);
					}
				}

				GameWindow* child = current->winGetChild();
				if (child != nullptr)
				{
					collectControlsRecursive(child, includeHidden, kind, controls);
				}
			}
		}

		nlohmann::json buildControlsInventory(const nlohmann::json& message)
		{
			bool includeHidden = false;
			std::string kind = "all";

			const auto argsIt = message.find("args");
			if (argsIt != message.end() && argsIt->is_object())
			{
				const auto hiddenIt = argsIt->find("include_hidden");
				if (hiddenIt != argsIt->end() && hiddenIt->is_boolean())
				{
					includeHidden = hiddenIt->get<bool>();
				}

				const auto kindIt = argsIt->find("kind");
				if (kindIt != argsIt->end() && kindIt->is_string())
				{
					kind = kindIt->get<std::string>();
				}
			}

			nlohmann::json controls = nlohmann::json::array();
			if (TheWindowManager != nullptr)
			{
				GameWindow* root = TheWindowManager->winGetWindowList();
				if (root != nullptr)
				{
					collectControlsRecursive(root, includeHidden, kind, controls);
				}
			}

			nlohmann::json result = {
				{"kind", kind},
				{"include_hidden", includeHidden},
				{"count", controls.size()},
				{"controls", controls}
			};
			return result;
		}

		bool executeChatSend(const nlohmann::json& message, std::string& reason)
		{
			if (TheNetwork == nullptr)
			{
				reason = "network_not_ready";
				return false;
			}
			if (ThePlayerList == nullptr || TheNameKeyGenerator == nullptr)
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

			const std::string text = getJsonString(*argsIt, "text");
			std::string scope = getJsonString(*argsIt, "scope");
			if (scope.empty())
			{
				scope = "everyone";
			}

			AsciiString asciiText(text.c_str());
			UnicodeString msg;
			msg.translate(asciiText);
			msg.trim();
			if (msg.isEmpty())
			{
				reason = "missing_text";
				return false;
			}

			const Player* localPlayer = ThePlayerList->getLocalPlayer();
			if (localPlayer == nullptr)
			{
				reason = "local_player_missing";
				return false;
			}

			Int playerMask = 0;
			AsciiString playerName;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				playerName.format("player%d", i);
				const Player* player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
				if (player == nullptr)
				{
					continue;
				}

				if (scope == "everyone")
				{
					if (TheGameInfo == nullptr || !TheGameInfo->getConstSlot(i)->isMuted())
					{
						playerMask |= (1 << i);
					}
				}
				else if (scope == "allies")
				{
					if ((player->getRelationship(localPlayer->getDefaultTeam()) == ALLIES &&
						localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) || player == localPlayer)
					{
						playerMask |= (1 << i);
					}
				}
				else if (scope == "players")
				{
					if (player == localPlayer)
					{
						playerMask |= (1 << i);
					}
				}
				else
				{
					reason = "invalid_scope";
					return false;
				}
			}

			if (TheLanguageFilter != nullptr)
			{
				TheLanguageFilter->filterLine(msg);
			}

			TheNetwork->sendChat(msg, playerMask);
			return true;
		}

		bool executeMenuClick(const nlohmann::json& message, std::string& reason)
		{
			if (TheShell == nullptr || !TheShell->isShellActive())
			{
				reason = "shell_not_active";
				return false;
			}

			if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
			{
				reason = "ui_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string controlId = getJsonString(*argsIt, "controlId");
			if (controlId.empty())
			{
				reason = "missing_controlId";
				return false;
			}

			const std::string normalizedControlId = normalizeControlId(controlId);
			const NameKeyType key = TheNameKeyGenerator->nameToKey(normalizedControlId.c_str());
			GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);

			// Context-aware back aliasing for menus that use different Back control names.
			if (control == nullptr && (controlId == "ButtonBack" || controlId == "MainMenu.wnd:ButtonBack"))
			{
				control = findFirstUsableControl({
					"MainMenu.wnd:ButtonSingleBack",
					"MainMenu.wnd:ButtonMultiBack",
					"MainMenu.wnd:ButtonLoadReplayBack",
					"MainMenu.wnd:ButtonDiffBack",
					"LanLobbyMenu.wnd:ButtonBack",
					"NetworkDirectConnect.wnd:ButtonBack"
				});
			}
			if (control == nullptr)
			{
				reason = "control_not_found";
				return false;
			}

			const UnsignedInt status = control->winGetStatus();
			if ((status & WIN_STATUS_HIDDEN) != 0u)
			{
				reason = "control_hidden";
				return false;
			}

			if ((status & WIN_STATUS_ENABLED) == 0u)
			{
				reason = "control_disabled";
				return false;
			}

			GameWindow* parent = control->winGetParent();
			if (parent == nullptr)
			{
				reason = "control_parent_missing";
				return false;
			}

			TheWindowManager->winSendSystemMsg(parent, GBM_SELECTED, (WindowMsgData)control, control->winGetWindowId());
			return true;
		}

		bool executeMenuSetText(const nlohmann::json& message, std::string& reason)
		{
			if (TheShell == nullptr || !TheShell->isShellActive())
			{
				reason = "shell_not_active";
				return false;
			}

			if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
			{
				reason = "ui_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string controlId = getJsonString(*argsIt, "controlId");
			const std::string text = getJsonString(*argsIt, "text");
			if (controlId.empty())
			{
				reason = "missing_controlId";
				return false;
			}

			const std::string normalizedControlId = normalizeControlId(controlId);
			const NameKeyType key = TheNameKeyGenerator->nameToKey(normalizedControlId.c_str());
			GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);
			if (control == nullptr)
			{
				reason = "control_not_found";
				return false;
			}

			const UnsignedInt status = control->winGetStatus();
			if ((status & WIN_STATUS_HIDDEN) != 0u)
			{
				reason = "control_hidden";
				return false;
			}

			if ((status & WIN_STATUS_ENABLED) == 0u)
			{
				reason = "control_disabled";
				return false;
			}

			const UnsignedInt style = control->winGetStyle();
			if ((style & GWS_ENTRY_FIELD) == 0u)
			{
				reason = "control_not_text_entry";
				return false;
			}

			AsciiString asciiText(text.c_str());
			UnicodeString unicodeText;
			unicodeText.translate(asciiText);
			GadgetTextEntrySetText(control, unicodeText);
			return true;
		}

		bool executeMenuSelectComboBox(const nlohmann::json& message, std::string& reason)
		{
			if (TheShell == nullptr || !TheShell->isShellActive())
			{
				reason = "shell_not_active";
				return false;
			}

			if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
			{
				reason = "ui_not_ready";
				return false;
			}

			const auto argsIt = message.find("args");
			if (argsIt == message.end() || !argsIt->is_object())
			{
				reason = "missing_args";
				return false;
			}

			const std::string controlId = getJsonString(*argsIt, "controlId");
			if (controlId.empty())
			{
				reason = "missing_controlId";
				return false;
			}

			const auto indexIt = argsIt->find("index");
			if (indexIt == argsIt->end() || !indexIt->is_number_integer())
			{
				reason = "missing_index";
				return false;
			}

			const Int selectedIndex = indexIt->get<Int>();

			const std::string normalizedControlId = normalizeControlId(controlId);
			const NameKeyType key = TheNameKeyGenerator->nameToKey(normalizedControlId.c_str());
			GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);
			if (control == nullptr)
			{
				reason = "control_not_found";
				return false;
			}

			const UnsignedInt status = control->winGetStatus();
			if ((status & WIN_STATUS_HIDDEN) != 0u)
			{
				reason = "control_hidden";
				return false;
			}

			if ((status & WIN_STATUS_ENABLED) == 0u)
			{
				reason = "control_disabled";
				return false;
			}

			const UnsignedInt style = control->winGetStyle();
			if ((style & GWS_COMBO_BOX) == 0u)
			{
				reason = "control_not_combo_box";
				return false;
			}

			// Check if index is valid
			const Int itemCount = GadgetComboBoxGetLength(control);
			if (selectedIndex < 0 || selectedIndex >= itemCount)
			{
				reason = "index_out_of_range";
				return false;
			}

			// Set the selection (updates UI)
			GadgetComboBoxSetSelectedPos(control, selectedIndex, TRUE);

			// Send GCM_SELECTED message to trigger the callback (same as user clicking)
			GameWindow* parent = control->winGetParent();
			if (parent != nullptr)
			{
				TheWindowManager->winSendSystemMsg(parent, GCM_SELECTED, (WindowMsgData)control, control->winGetWindowId());
			}

			return true;
		}

	bool executeMenuSetSlider(const nlohmann::json& message, std::string& reason)
	{
		if (TheShell == nullptr || !TheShell->isShellActive())
		{
			reason = "shell_not_active";
			return false;
		}

		if (TheWindowManager == nullptr || TheNameKeyGenerator == nullptr)
		{
			reason = "ui_not_ready";
			return false;
		}

		const auto argsIt = message.find("args");
		if (argsIt == message.end() || !argsIt->is_object())
		{
			reason = "missing_args";
			return false;
		}

		const std::string controlId = getJsonString(*argsIt, "controlId");
		if (controlId.empty())
		{
			reason = "missing_controlId";
			return false;
		}

		const auto valueIt = argsIt->find("value");
		if (valueIt == argsIt->end() || !valueIt->is_number_integer())
		{
			reason = "missing_value";
			return false;
		}

		const Int sliderValue = valueIt->get<Int>();

		const std::string normalizedControlId = normalizeControlId(controlId);
		const NameKeyType key = TheNameKeyGenerator->nameToKey(normalizedControlId.c_str());
		GameWindow* control = TheWindowManager->winGetWindowFromId(nullptr, key);
		if (control == nullptr)
		{
			reason = "control_not_found";
			return false;
		}

		const UnsignedInt status = control->winGetStatus();
		if ((status & WIN_STATUS_HIDDEN) != 0u)
		{
			reason = "control_hidden";
			return false;
		}

		if ((status & WIN_STATUS_ENABLED) == 0u)
		{
			reason = "control_disabled";
			return false;
		}

		// Set the slider position (sends GSM_SET_SLIDER to the slider)
		GadgetSliderSetPosition(control, sliderValue);

		// Send GSM_SLIDER_TRACK message to the owner (like the slider does when dragged)
		GameWindow* owner = control->winGetOwner();
		if (owner != nullptr)
		{
			TheWindowManager->winSendSystemMsg(owner, GSM_SLIDER_TRACK, (WindowMsgData)control, (WindowMsgData)sliderValue);
		}

		return true;
	}
