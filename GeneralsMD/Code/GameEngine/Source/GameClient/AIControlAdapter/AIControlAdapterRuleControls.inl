// Included inside AIControlAdapterState. Profile defaults plus operator patches.
        struct RuleDescriptor { const char* id; const char* label; const char* description; bool restriction; };

        static const std::vector<RuleDescriptor>& autonomyRuleCatalog()
        {
            static const std::vector<RuleDescriptor> catalog = {
                {"market_growth", "Black Markets", "New income structures in existing zones", false},
                {"zone_expansion", "Zone expansion", "New supply zones and strategic footholds", false},
                {"capacity_growth", "Factory growth", "Additional factories and barracks; missing producers may recover", false},
                {"combat_production", "Combat production", "Conventional combat recruitment; siege has its own switch", false},
                {"siege_production", "SCUD Launchers", "Mobile SCUD Launcher recruitment and replacement", false},
                {"worker_recruitment", "Worker recruitment", "New workers from all automatic recruitment paths", false},
                {"worker_redistribution", "Worker redistribution", "New transfers of idle workers between existing zones", false},
                {"static_defense", "Static defenses", "New defensive Tunnel Networks and Stinger Sites", false},
                {"raids", "Raids", "New limited offensive raids before the economic milestone", false},
                {"sustained_assaults", "Sustained assaults", "New persistent offensive groups after the economic milestone", false},
                {"scouting", "Scouting", "New scout missions and scout/radar recruitment", false},
                {"capture", "Capture", "New tech-building capture missions and capture-source recruitment", false},
                {"garrisons", "Garrisons", "New defensive garrison assignments", false},
                {"counterbattery", "Counterbattery", "New mobile responses to enemy artillery", false},
                {"storm_construction", "SCUD Storm construction", "New Storm builds; superweapon permission still required", false},
                {"storm_firing", "SCUD Storm firing", "Automatic Storm firing; superweapon permission still required", false},
                {"tech_upgrades", "Tech and upgrades", "Palaces, science and automatic upgrades", false},
                {"idle_guard", "Idle guard", "Automatic orders to idle ground combat units", false},
                {"defer_income_under_threat", "Defer income under attack", "Economic profile restricts market growth during threat response", true},
                {"defer_offense_under_threat", "Defer offense under attack", "Require relief from local pressure for economic offense", true}
            };
            return catalog;
        }

        bool autonomyRuleDefault(const std::string& id) const
        {
            if (id == "defer_income_under_threat") return resolveAutonomyProfilePolicyConfig().isEconomyFirstSprawl;
            if (id == "defer_offense_under_threat") return true;
            if (m_autonomy.state.profile == "builtin_passthrough") return false;
            if (id == "capture") return m_autonomy.state.captureTech;
            if (id == "garrisons") return m_autonomy.state.garrisonsEnabled;
            if (id == "raids" || id == "sustained_assaults")
                return !m_autonomy.state.hasAttackAutomationEnabledOverride || m_autonomy.state.attackAutomationEnabled;
            return true;
        }

        bool autonomyRuleEnabled(const std::string& id) const
        {
            const auto it = m_autonomy.state.ruleOverrides.find(id);
            return it == m_autonomy.state.ruleOverrides.end() ? autonomyRuleDefault(id) : it->second;
        }

        bool requireAutonomyRule(const char* id, std::string& reason)
        {
            if (autonomyRuleEnabled(id)) return true;
            reason = std::string("rule_disabled:") + id;
            m_autonomy.state.ruleBlocks[id] = {reason, adapterGameplayTick()};
            return false;
        }

        nlohmann::json buildAutonomyRuleState() const
        {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& rule : autonomyRuleCatalog())
            {
                const auto it = m_autonomy.state.ruleOverrides.find(rule.id);
                const bool enabled = autonomyRuleEnabled(rule.id);
                std::string gate = !enabled ? "rule_disabled" : !isAutonomyModeActive() ? "controller_inactive"
                    : !areAutonomySimCommandsEnabled() ? "sim_commands_disabled"
                    : ((std::strcmp(rule.id, "storm_construction") == 0 || std::strcmp(rule.id, "storm_firing") == 0)
                        && !m_autonomy.state.allowSuperweapons) ? "superweapons_disabled" : "evaluation_enabled";
                if (gate == "evaluation_enabled" && !rule.restriction)
                {
                    const std::string id = rule.id;
                    if (id == "market_growth" && m_autonomy.state.profileSpendPlan.active
                        && m_autonomy.state.profileSpendPlan.threatResponse && autonomyRuleEnabled("defer_income_under_threat")
                        && m_autonomy.state.profileCompletedMarkets > 0) gate = "income_deferred_under_threat";
                    else if (id == "sustained_assaults" && !m_autonomy.state.economicVictory) gate = "economic_threshold_pending";
                    else if (id == "sustained_assaults" && autonomyRuleEnabled("defer_offense_under_threat")
                        && !economicAssaultReady(adapterGameplayTick())) gate = "defense_pressure";
                    else if (id == "raids" && resolveAutonomyProfilePolicyConfig().isEconomyFirstSprawl
                        && !m_autonomy.state.economicVictory) gate = "economic_profile_waits_for_milestone";
                }
                const auto block = m_autonomy.state.ruleBlocks.find(rule.id);
                rows.push_back({{"id", rule.id}, {"label", rule.label}, {"description", rule.description},
                    {"kind", rule.restriction ? "restriction" : "behavior"},
                    {"profile_default", autonomyRuleDefault(rule.id)},
                    {"override", it == m_autonomy.state.ruleOverrides.end() ? nlohmann::json(nullptr) : nlohmann::json(it->second)},
                    {"effective", enabled}, {"gate_reason", gate},
                    {"last_block_reason", block == m_autonomy.state.ruleBlocks.end() ? "" : block->second.first},
                    {"last_block_tick", block == m_autonomy.state.ruleBlocks.end() ? 0u : block->second.second}});
            }
            return {{"version", 1}, {"session_id", m_log.sessionId()}, {"revision", m_autonomy.state.ruleRevision},
                {"changed_tick", m_autonomy.state.ruleChangedTick}, {"rules", rows}};
        }

        bool validateAutonomyRulePatch(const nlohmann::json& args, std::string& reason) const
        {
            const auto it = args.find("rule_overrides");
            if (it == args.end()) return true;
            if (!it->is_object()) { reason = "invalid_rule_overrides"; return false; }
            for (auto entry = it->begin(); entry != it->end(); ++entry)
            {
                bool known = false;
                for (const auto& rule : autonomyRuleCatalog()) if (entry.key() == rule.id) known = true;
                if (!known) { reason = "unknown_rule:" + entry.key(); return false; }
                if (!entry.value().is_boolean() && !entry.value().is_null())
                { reason = "invalid_rule_value:" + entry.key(); return false; }
            }
            return true;
        }

        bool validateAutonomyConfigurePatch(const nlohmann::json& args, std::string& reason) const
        {
            if (!validateAutonomyRulePatch(args, reason)) return false;
            const auto scope = args.find("rule_scope");
            if (scope != args.end())
            {
                Player* player = resolveAutonomyPlayer();
                if (!scope->is_object() || !scope->contains("session_id") || !(*scope)["session_id"].is_string()
                    || !scope->contains("match_generation") || !(*scope)["match_generation"].is_number_integer()
                    || !scope->contains("player_index") || !(*scope)["player_index"].is_number_integer())
                { reason = "invalid_rule_scope"; return false; }
                if ((*scope)["session_id"] != m_log.sessionId() || (*scope)["match_generation"] != m_recordingJournal.generation
                    || player == nullptr || (*scope)["player_index"] != player->getPlayerIndex())
                { reason = "rule_scope_changed"; return false; }
            }
            for (const char* key : {"capture_tech", "attack_enabled", "debug_draw", "allow_superweapons",
                "sim_commands_enabled", "worker_shuttles_enabled", "garrisons_enabled", "technical_scout_shuttles_enabled",
                "allow_expansion_before_full_remote_followup"})
            {
                const auto it = args.find(key);
                if (it != args.end() && !it->is_boolean()) { reason = std::string("invalid_") + key; return false; }
            }
            for (const char* key : {"economy_bias", "aggression_bias", "defense_bias", "expansion_bias", "sprawl_multiplier", "zone_radius"})
            {
                const auto it = args.find(key);
                if (it != args.end() && (!it->is_number() || !std::isfinite(it->get<double>())))
                { reason = std::string("invalid_") + key; return false; }
            }
            for (const char* key : {"urgent_zone_gap_threshold", "max_concurrent_expansion_stashes",
                "expansion_high_cash_float_threshold", "player_index", "target_player_index"})
            {
                const auto it = args.find(key);
                if (it != args.end() && (!it->is_number_integer() || it->get<double>() < -2147483648.0
                    || it->get<double>() > 2147483647.0))
                { reason = std::string("invalid_") + key; return false; }
            }
            const auto cash = args.find("expansion_high_cash_float_threshold");
            if (cash != args.end() && cash->get<Int>() < 0) { reason = "invalid_expansion_high_cash_float_threshold"; return false; }
            return true;
        }

        void filterAutonomyMacroRules(AIControlAdapterMacroBuildPlan& plan, int arms, int barracks)
        {
            std::vector<AIControlAdapterMacroBuildIntentOption> options;
            for (auto& intent : plan.intents)
            {
                const char* rule = nullptr;
                const std::string cmd = intent.option.command != nullptr ? intent.option.command : "";
                if (cmd == "Game.BuildBlackMarketSmart") rule = "market_growth";
                else if (cmd == "Game.BuildSupplyStashSmart") rule = "zone_expansion";
                else if (cmd == "Game.BuildPalaceSmart") rule = "tech_upgrades";
                else if (cmd == "Game.BuildArmsDealerSmart" && arms > 0) rule = "capacity_growth";
                else if (cmd == "Game.BuildBarracksSmart" && barracks > 0) rule = "capacity_growth";
                else if (cmd == "Game.BuildTunnelNetwork" || cmd == "Game.BuildStingerSite") rule = "static_defense";
                if (rule != nullptr && !autonomyRuleEnabled(rule))
                {
                    intent.option.valid = false;
                    intent.option.reason = "rule_disabled";
                }
                options.push_back(intent.option);
            }
            plan.choice = AIControlAdapterChooseMacroBuildIntent(options);
        }

        void applyAutonomyRulePatch(const nlohmann::json& args, const std::string& requestId)
        {
            const auto it = args.find("rule_overrides");
            if (it == args.end()) return;
            bool changed = false;
            for (auto entry = it->begin(); entry != it->end(); ++entry)
            {
                const auto old = m_autonomy.state.ruleOverrides.find(entry.key());
                const nlohmann::json prior = old == m_autonomy.state.ruleOverrides.end() ? nlohmann::json(nullptr) : nlohmann::json(old->second);
                if (prior == entry.value()) continue;
                const bool oldEffective = autonomyRuleEnabled(entry.key());
                if (entry.value().is_null()) m_autonomy.state.ruleOverrides.erase(entry.key());
                else m_autonomy.state.ruleOverrides[entry.key()] = entry.value().get<bool>();
                m_autonomy.state.ruleBlocks.erase(entry.key());
                const std::string detail = prior.dump() + "->" + entry.value().dump() + ";effective="
                    + (oldEffective ? "true" : "false") + "->" + (autonomyRuleEnabled(entry.key()) ? "true" : "false");
                appendRecordingEvent("rule", entry.key().c_str(), detail.c_str(), 0, 0, "operator", requestId.c_str(), "changed");
                changed = true;
            }
            if (changed)
            {
                ++m_autonomy.state.ruleRevision;
                m_autonomy.state.ruleChangedTick = adapterGameplayTick();
                m_recordingSnapshotCache = nlohmann::json();
            }
        }

        // Expansion-off constrains new automated growth, not observed anchors.
        bool withinExistingAutonomyFootprint(const Coord3D& location) const
        {
            if (!m_autonomy.state.telemetryZones.is_array()) return false;
            for (const auto& zone : m_autonomy.state.telemetryZones)
            {
                const Real dx = location.x - zone.value("center_x", 0.0f);
                const Real dy = location.y - zone.value("center_y", 0.0f);
                const Real radius = zone.value("radius", m_autonomy.state.zoneRadius);
                if (dx * dx + dy * dy <= radius * radius) return true;
            }
            return false;
        }

        bool autonomousConstructionRuleAllowed(Player* player, const ThingTemplate* templ,
            const Coord3D& location, std::string& reason)
        {
            if (!m_profileAutomaticSpendScope || !isAutonomyModeActive() || player != resolveAutonomyPlayer()) return true;
            const std::string name = templ->getName().str();
            const char* rule = nullptr;
            if (containsIgnoreCase(name, "blackmarket")) rule = "market_growth";
            else if (containsIgnoreCase(name, "supply")) rule = "zone_expansion";
            else if (containsIgnoreCase(name, "palace")) rule = "tech_upgrades";
            else if (containsIgnoreCase(name, "scudstorm")) rule = "storm_construction";
            else if (containsIgnoreCase(name, "tunnel") || containsIgnoreCase(name, "stinger"))
                rule = m_ruleExpansionConstructionScope ? "zone_expansion" : "static_defense";
            else if (containsIgnoreCase(name, "armsdealer") || containsIgnoreCase(name, "barracks") || containsIgnoreCase(name, "commandcenter"))
            {
                // Recovery of the first producer is distinct from additional capacity.
                std::vector<AutomationOwnedObjectSnapshot> owned;
                collectOwnedAutomationObjects(player, owned);
                for (const auto& object : owned) if (object.object != nullptr && object.name == name) rule = "capacity_growth";
                for (const auto* task : m_autonomy.taskReservationManager.findBuildTasks(name))
                    if (task->targetObjectId == 0u) rule = "capacity_growth";
            }
            if (rule != nullptr && !requireAutonomyRule(rule, reason)) return false;
            if (!autonomyRuleEnabled("zone_expansion") && !withinExistingAutonomyFootprint(location))
            {
                reason = "no_existing_zone_space";
                m_autonomy.state.ruleBlocks[rule != nullptr ? rule : "zone_expansion"] = {reason, adapterGameplayTick()};
                return false;
            }
            return true;
        }

        bool autonomousUnitRuleAllowed(Player* player, const ThingTemplate* templ, std::string& reason, const char* purpose)
        {
            if (!m_profileAutomaticSpendScope || !isAutonomyModeActive() || player != resolveAutonomyPlayer()) return true;
            if (std::strcmp(purpose, "capture") == 0) return requireAutonomyRule("capture", reason);
            const std::string name = templ->getName().str();
            if (templ->isKindOf(KINDOF_DOZER)) return requireAutonomyRule("worker_recruitment", reason);
            if (containsIgnoreCase(name, "scudlauncher")) return requireAutonomyRule("siege_production", reason);
            if (containsIgnoreCase(name, "radarvan")) return requireAutonomyRule("scouting", reason);
            // Technicals also transport builders/garrisons; their callers gate purpose.
            if (containsIgnoreCase(name, "technical")) return true;
            return !templ->isKindOf(KINDOF_CAN_ATTACK) || requireAutonomyRule("combat_production", reason);
        }
