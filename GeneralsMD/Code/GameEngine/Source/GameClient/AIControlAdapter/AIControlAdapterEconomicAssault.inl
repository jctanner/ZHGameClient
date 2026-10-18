// Included inside AIControlAdapterState. Economic strength is a match milestone,
// not a minimum balance that must be hoarded after production starts.
		void resetEconomicVictory()
		{
			m_autonomy.state.profileSpendPlan = {};
			m_autonomy.state.profileThreatUntilTick = 0u;
			m_autonomy.state.profileWavePeak = 0;
			m_autonomy.state.profileWavePeakTick = 0u;
			m_autonomy.state.profileArmsDealerTarget = 0;
			m_autonomy.state.profileBarracksTarget = 0;
			m_autonomy.state.profileOffenseFundedSinceTick = 0u;
			m_autonomy.state.profileAvailableCash = 0u;
			m_autonomy.state.profileConstructionCommittedCash = 0u;
			m_autonomy.state.profileUnitCommittedCash = 0u;
			m_autonomy.state.profileCompletedMarkets = 0;
			m_autonomy.state.profileArmyObserved = 0;
			m_autonomy.state.profilePlanTick = 0u;
			m_autonomy.state.economicVictoryReason = "pending";
			m_autonomy.state.economicVictory = false;
			m_autonomy.state.economicVictoryPlayerIndex = -1;
			m_autonomy.state.economicVictoryTick = 0u;
			m_autonomy.state.nextAssaultTick = 0u;
			m_autonomy.state.searchedAssaultObjectives.clear();
			m_autonomy.state.deferredAssaultObjectives.clear();
		}


		bool isSurvivalRecoveryActive(DWORD now) const
		{
			return isAutonomyModeActive() && resolveAutonomyProfilePolicyConfig().isBalancedSprawl
				&& AIControlAdapterIsTickInFuture(m_autonomy.state.survivalRecoveryUntilTick, now);
		}

		bool isConsolidatedSurvivalZone(UnsignedInt anchor, DWORD now) const
		{
			return isSurvivalRecoveryActive(now) && !m_autonomy.state.survivalCoreZones.empty()
				&& m_autonomy.state.survivalCoreZones.count(anchor) == 0u;
		}

		bool survivalRallyPosition(Coord3D& position) const
		{
			if (!isSurvivalRecoveryActive(adapterGameplayTick())) return false;
			for (const auto& zone : m_autonomy.state.telemetryZones)
				if (zone.value("anchor_id", 0u) == m_autonomy.state.recoveryZoneAnchor)
				{
					position = {zone.value("front_point_x", zone.value("center_x", 0.0f)),
						zone.value("front_point_y", zone.value("center_y", 0.0f)), 0.0f};
					return true;
				}
			return false;
		}

		bool isEconomicVictoryPhase() const
		{
			return isAutonomyModeActive() && m_autonomy.state.economicVictory
				&& resolveAutonomyProfilePolicyConfig().isBalancedSprawl;
		}

		void updateEconomicVictory(Player* player, UnsignedInt money, DWORD now)
		{
			if (m_autonomy.state.economicVictoryPlayerIndex != player->getPlayerIndex())
			{
				resetEconomicVictory();
				m_autonomy.state.economicVictoryPlayerIndex = player->getPlayerIndex();
			}
			if (!m_autonomy.state.economicVictory && money >= 100000u
				&& resolveAutonomyProfilePolicyConfig().isBalancedSprawl)
			{
				m_autonomy.state.economicVictory = true;
				m_autonomy.state.economicVictoryTick = now;
				m_autonomy.state.nextAssaultTick = 0u;
				m_autonomy.state.economicVictoryReason = "cash_milestone";
			recordAutonomyTelemetryEvent("strategy", "economic_victory", "cash_milestone", nullptr);
				adapterLog("economic_victory player=%d tick=%u money=%u army_target=1000 markets=120",
					player->getPlayerIndex(), now, money);
			}
		}

		UnsignedInt mainAutonomyZoneAnchorId() const
		{
			if (!m_autonomy.state.telemetryZones.is_array()) return 0u;
			for (const auto& zone : m_autonomy.state.telemetryZones)
				if (zone.is_object() && zone.value("is_main_base", false)) return zone.value("anchor_id", 0u);
			return 0u;
		}

		bool economicAssaultReady(DWORD now) const
		{
			if (!isEconomicVictoryPhase() || !m_automation.attackRule.enabled || !autonomyRuleEnabled("sustained_assaults") || !areAutonomySimCommandsEnabled()) return false;
			if (!autonomyRuleEnabled("defer_offense_under_threat")) return true;
			// With a large surplus only main-base pressure defers offense; zone
			// defense tasks answer outer-zone threats from reserved units.
			const bool surplus = m_autonomy.state.profileSpendPlan.active && m_autonomy.state.profileSpendPlan.surplusUnderThreat;
			if (m_autonomy.state.profileSpendPlan.active && m_autonomy.state.profileSpendPlan.threatResponse && !surplus) return false;
			const UnsignedInt mainAnchor = mainAutonomyZoneAnchorId();
			for (const auto& entry : m_autonomy.state.zoneThreats)
			{
				const auto& threat = entry.second;
				if (now - threat.lastSeenTick > 10000u) continue;
				if (surplus && mainAnchor != 0u && entry.first != mainAnchor) continue;
				if (threat.localEnemyCount > 0 || threat.enemyArtilleryCount > 0
					|| AIControlAdapterDefenseManager::ThreatSeverityForLevel(threat.level) >= 3) return false;
			}
			return true;
		}

		UnsignedInt availableAutonomyCash(Player* player)
		{
			reconcilePendingUnitQueues();
			unsigned long long committed = 0u;
			for (const auto& pending : m_pendingUnitQueues)
				if (pending.playerIndex == player->getPlayerIndex()) committed += pending.cost;
			const Money* wallet = player->getMoney();
			const UnsignedInt cash = wallet != nullptr ? wallet->countMoney() : 0u;
			if (resolveAutonomyProfilePolicyConfig().isEconomyFirstSprawl)
			{
				m_autonomy.state.profileUnitCommittedCash = static_cast<unsigned int>(std::min<unsigned long long>(committed, cash));
				const auto unitCommitments = committed;
				// Unstarted sites have not charged their cost yet. Release the claim
				// once native construction is observed or reservation lifecycle ends.
				for (const auto* task : m_autonomy.taskReservationManager.findBuildTasks())
				{
					if (task->targetObjectId != 0u) continue;
					Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->sourceObjectId));
					if (worker == nullptr || worker->isEffectivelyDead() || worker->getControllingPlayer() != player) continue;
					const auto* templ = TheThingFactory->findTemplate(AsciiString(task->expectedTemplate.c_str()), false);
					if (templ != nullptr) committed += static_cast<unsigned int>(std::max<Int>(0, templ->calcCostToBuild(player)));
				}
				m_autonomy.state.profileConstructionCommittedCash = static_cast<unsigned int>(std::min<unsigned long long>(committed - unitCommitments, cash));
				m_autonomy.state.profileAvailableCash = committed < cash ? cash - static_cast<UnsignedInt>(committed) : 0u;
			}
			return committed < cash ? cash - static_cast<UnsignedInt>(committed) : 0u;
		}

		void updateProfileSpendingPlan(Player* player, UnsignedInt cash, int completedMarkets, int mobileArmy, DWORD now)
		{
			const auto config = resolveAutonomyProfilePolicyConfig();
			if (!config.isEconomyFirstSprawl) { m_autonomy.state.profileSpendPlan = {}; return; }
			int approaching = 0;
			// Reuse visible enemy memory and current owned-zone geometry. No hidden
			// object positions or extra world scan; this runs on macro cadence.
			const Real radius = std::max<Real>(900.0f, m_autonomy.state.zoneRadius * 2.5f);
			for (const auto& item : m_autonomy.enemyMemory.getItems())
			{
				if (item.isStructure || item.stale || item.lastSeenTick == 0u || now - item.lastSeenTick > 5000u) continue;
				Player* enemy = getPlayerByIndex(item.playerIndex);
				if (enemy == nullptr || enemy->getDefaultTeam() == nullptr || player->getRelationship(enemy->getDefaultTeam()) != ENEMIES) continue;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					const Real dx = item.position.x - zone.value("center_x", 0.0f);
					const Real dy = item.position.y - zone.value("center_y", 0.0f);
					if (dx * dx + dy * dy <= radius * radius) { ++approaching; break; }
				}
			}
			bool damaged = false;
			for (const auto& entry : m_autonomy.state.zoneThreats)
			{
				const auto& threat = entry.second;
				if (now - threat.lastSeenTick > 5000u) continue;
				approaching = std::max(approaching, threat.localEnemyCount + threat.enemyArtilleryCount);
				damaged = damaged || threat.damagedStructures > 0 || threat.destroyedStructures > 0;
			}
			if (approaching > 0 || damaged || isSurvivalRecoveryActive(now))
				m_autonomy.state.profileThreatUntilTick = now + config.threatHoldMs;
			const bool response = AIControlAdapterIsTickInFuture(m_autonomy.state.profileThreatUntilTick, now);
			const bool funded = (!response || !autonomyRuleEnabled("defer_offense_under_threat")) && completedMarkets >= config.offensiveMarketThreshold
				&& cash >= config.reserveCash + config.offensiveCashFloat;
			if (!funded) m_autonomy.state.profileOffenseFundedSinceTick = 0u;
			else if (m_autonomy.state.profileOffenseFundedSinceTick == 0u) m_autonomy.state.profileOffenseFundedSinceTick = now;
			else if (!m_autonomy.state.economicVictory && now - m_autonomy.state.profileOffenseFundedSinceTick >= 30000u)
			{
				m_autonomy.state.economicVictory = true;
				m_autonomy.state.economicVictoryTick = now;
				m_autonomy.state.economicVictoryReason = "income_capacity";
				m_autonomy.state.nextAssaultTick = 0u;
				recordAutonomyTelemetryEvent("strategy", "economic_victory", "income_capacity", nullptr);
			}
			m_autonomy.state.profilePlanTick = now;
			m_autonomy.state.profileCompletedMarkets = completedMarkets;
			m_autonomy.state.profileArmyObserved = mobileArmy;
			m_autonomy.state.profileSpendPlan = AIControlAdapterProfilePolicyManager().ResolveSpendPlan(
				config, cash, completedMarkets, approaching, response, m_autonomy.state.economicVictory);
			m_autonomy.state.profileSpendPlan.deferIncomeUnderThreat = autonomyRuleEnabled("defer_income_under_threat");
			// Size the standing army to recent attack waves: hold the peak for 4 minutes,
			// then decay it over the next 4, so production keeps refilling between waves.
			if (approaching >= m_autonomy.state.profileWavePeak)
			{
				m_autonomy.state.profileWavePeak = approaching;
				m_autonomy.state.profileWavePeakTick = now;
			}
			int wave = m_autonomy.state.profileWavePeak;
			const DWORD waveAge = now - m_autonomy.state.profileWavePeakTick;
			if (waveAge > 240000u)
			{
				const DWORD decayMs = std::min<DWORD>(waveAge - 240000u, 240000u);
				wave = static_cast<int>(wave * static_cast<long long>(240000u - decayMs) / 240000);
				if (wave <= approaching) { m_autonomy.state.profileWavePeak = approaching; m_autonomy.state.profileWavePeakTick = now; wave = approaching; }
			}
			AIControlAdapterProfileSpendPlan& plan = m_autonomy.state.profileSpendPlan;
			plan.armyFloor = std::min(150, (wave * 5 + 3) / 4);
			plan.armyFloorMet = mobileArmy >= plan.armyFloor;
			// Surplus mode: workers scale with territory (they are builders, not income).
			plan.surplusMode = cash >= config.surplusModeCash;
			if (plan.surplusMode)
			{
				const int zoneCount = m_autonomy.state.telemetryZones.is_array() ? static_cast<int>(m_autonomy.state.telemetryZones.size()) : 0;
				plan.workerLimit = std::max(plan.workerLimit, std::min(config.surplusWorkerCap, 40 + 4 * zoneCount));
				plan.armyTarget = std::max(plan.armyFloor, std::min(plan.armyTarget, config.surplusArmyCap));
				plan.maxConcurrentMarkets = std::max(plan.maxConcurrentMarkets, 12);
			}
			if (!plan.sustainedOffense) plan.armyTarget = std::max(plan.armyTarget, plan.armyFloor);
		}

		int profilePendingCombatCount(Player* player) const
		{
			int count = 0;
			for (const auto& pending : m_pendingUnitQueues)
			{
				if (pending.playerIndex != player->getPlayerIndex()) continue;
				const auto* templ = TheThingFactory->findTemplate(AsciiString(pending.templateName.c_str()), false);
				if (templ != nullptr && templ->isKindOf(KINDOF_CAN_ATTACK) && !templ->isKindOf(KINDOF_DOZER)
					&& !containsIgnoreCase(pending.templateName, "technical")) ++count;
			}
			return count;
		}

		bool profileConstructionAllowed(Player* player, const ThingTemplate* templ, unsigned int cost, std::string& reason)
		{
			const auto& plan = m_autonomy.state.profileSpendPlan;
			if (!m_profileAutomaticSpendScope || !plan.active || player != resolveAutonomyPlayer()) return true;
			const std::string name = templ->getName().str();
			StrategicSpendCategory category = StrategicSpendCategory::LuxuryBaseline;
			if (containsIgnoreCase(name, "blackmarket")) category = StrategicSpendCategory::EconomyGrowth;
			else if (containsIgnoreCase(name, "supply")) category = StrategicSpendCategory::Expansion;
			else if (containsIgnoreCase(name, "palace")) category = StrategicSpendCategory::TechPrerequisite;
			else if (containsIgnoreCase(name, "scudstorm")) category = m_autonomy.wmdTargetTracker.hasActiveWMDThreat()
				? StrategicSpendCategory::DefensiveWmd : StrategicSpendCategory::LuxuryBaseline;
			else if (containsIgnoreCase(name, "tunnel") || containsIgnoreCase(name, "stinger")) category = StrategicSpendCategory::StaticDefense;
			else if (containsIgnoreCase(name, "armsdealer") || containsIgnoreCase(name, "barracks") || containsIgnoreCase(name, "commandcenter"))
			{
				int existing = 0;
				std::vector<AutomationOwnedObjectSnapshot> owned;
				collectOwnedAutomationObjects(player, owned);
				for (const auto& object : owned)
					if (object.object != nullptr && object.name == name) ++existing;
				for (const auto* task : m_autonomy.taskReservationManager.findBuildTasks(name))
					if (task->targetObjectId == 0u) ++existing;
				// Never undercut the producer targets the macro planner chose (they scale with
				// surplus cash and the army target); refusing here starved every other intent.
				const int target = containsIgnoreCase(name, "armsdealer")
					? std::max(std::min(8, 2 + m_autonomy.state.profileCompletedMarkets / 3), m_autonomy.state.profileArmsDealerTarget)
					: containsIgnoreCase(name, "barracks") ? std::max(2, m_autonomy.state.profileBarracksTarget)
					// Surplus mode: extra Command Centers produce workers (one per 50 of the limit).
					: plan.surplusMode ? std::max(1, (plan.workerLimit + 49) / 50) : 1;
				if (existing >= target) { reason = "profile_capacity_satisfied"; return false; }
				category = existing == 0 ? StrategicSpendCategory::ProducerRecovery : StrategicSpendCategory::ProductionGrowth;
			}
			AIControlAdapterStrategicSpendSnapshot snapshot;
			snapshot.profilePlan = plan;
			snapshot.money = availableAutonomyCash(player);
			snapshot.completedMarkets = m_autonomy.state.profileCompletedMarkets;
            for (const auto* task : m_autonomy.taskReservationManager.findBuildTasks())
                if (containsIgnoreCase(task->expectedTemplate, "blackmarket")) ++snapshot.healthyMarketsInProgress;
			snapshot.activeWmdThreats = m_autonomy.wmdTargetTracker.hasActiveWMDThreat() ? 1 : 0;
			snapshot.armySize = m_autonomy.state.profileArmyObserved;
			const auto decision = AIControlAdapterStrategicSpendManager().Evaluate(snapshot, category, cost);
			if (!decision.allowed) reason = decision.reason;
			return decision.allowed;
		}

		bool profileUnitAllowed(Player* player, Object* producer, const ThingTemplate* templ, unsigned int cost, std::string& reason)
		{
			const auto& plan = m_autonomy.state.profileSpendPlan;
			if (!m_profileAutomaticSpendScope || !plan.active || player != resolveAutonomyPlayer()) return true;
			const bool worker = templ->isKindOf(KINDOF_DOZER);
			unsigned int floor = plan.combatProtectedCash;
			if (worker)
			{
				int workers = 0;
				std::vector<AutomationOwnedObjectSnapshot> owned;
				collectOwnedAutomationObjects(player, owned);
				for (const auto& object : owned)
				{
					if (object.isDozer) ++workers;
					if (object.object == nullptr || object.underConstruction) continue;
					auto* queue = object.object->getProductionUpdateInterface();
					if (queue != nullptr) workers += static_cast<int>(queue->countUnitTypeInQueue(templ));
				}
				for (const auto& pending : m_pendingUnitQueues)
					if (pending.playerIndex == player->getPlayerIndex() && pending.templateName == templ->getName().str()) ++workers;
				if (workers >= plan.workerLimit) { reason = "profile_worker_limit"; return false; }
				floor = workers < 4 ? 1000u : plan.workerProtectedCash;
			}
			else if (templ->isKindOf(KINDOF_CAN_ATTACK) && !containsIgnoreCase(templ->getName().str(), "technical"))
			{
				const int pending = profilePendingCombatCount(player);
				if (m_autonomy.state.profileArmyObserved + pending >= plan.armyTarget)
				{ reason = "profile_army_target"; return false; }
			}
			if (availableAutonomyCash(player) < floor + cost) { reason = "profile_reserve_protected"; return false; }
			auto* queue = producer->getProductionUpdateInterface();
			int pendingDepth = 0;
			for (const auto& pending : m_pendingUnitQueues) if (pending.producerId == producer->getID()) ++pendingDepth;
			const int queueDepthLimit = worker && plan.surplusMode ? 4 : 2;
			if (queue == nullptr || queue->getProductionCount() + pendingDepth >= queueDepthLimit) { reason = "profile_shallow_queue_full"; return false; }
			return true;
		}

		bool economicAssaultTargetDeferred(UnsignedInt id, DWORD now) const
		{
			const auto it = m_autonomy.state.deferredAssaultObjectives.find(id);
			return it != m_autonomy.state.deferredAssaultObjectives.end()
				&& AIControlAdapterIsTickInFuture(it->second, now);
		}

		std::vector<Object*> economicAssaultRepresentatives(const std::vector<unsigned int>& ids) const
		{
			std::vector<Object*> result;
			std::set<const ThingTemplate*> types;
			for (unsigned int id : ids)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				if (unit != nullptr && !unit->isEffectivelyDead() && types.insert(unit->getTemplate()).second)
					result.push_back(unit);
				if (result.size() >= 8u) break;
			}
			return result;
		}

		bool economicAssaultCanAttack(const std::vector<Object*>& units, Object* target) const
		{
			if (TheActionManager == nullptr) return false;
			for (Object* unit : units)
				if (TheActionManager->getCanAttackObject(unit, target, CMD_FROM_PLAYER, ATTACK_NEW_TARGET)
					>= ATTACKRESULT_POSSIBLE_AFTER_MOVING) return true;
			return false;
		}

		// Visible objects are checked for group attack capability; fogged objectives use only the
		// stored position, never a hidden object's current location or health.
		bool chooseEconomicAssaultTarget(Player* player, const Coord3D& center,
			UnsignedInt& targetId, Coord3D& targetPosition, const std::vector<unsigned int>& units,
			UnsignedInt requestingTask = 0u)
		{
			const DWORD now = adapterGameplayTick();
			const auto representatives = economicAssaultRepresentatives(units);
			std::map<UnsignedInt, int> commitments;
			for (const CombatTask* task : m_autonomy.combatTaskManager.findAttackTasks())
				if (task->owner == "autonomous_assault" && task->taskId != requestingTask)
					commitments[task->targetObjectId] += static_cast<int>(task->assignedUnitIds.size());
			Object* best = nullptr;
			Real bestScore = 1.0e30f;
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (!isEnemyControlledObject(player, obj) || obj->getPosition() == nullptr) continue;
				const auto shroud = obj->getShroudedStatus(player->getPlayerIndex());
				if (shroud != OBJECTSHROUD_CLEAR && shroud != OBJECTSHROUD_PARTIAL_CLEAR) continue;
				if (obj->isEffectivelyDead() || obj->getContainedBy() != nullptr) continue;
				if (economicAssaultTargetDeferred(static_cast<UnsignedInt>(obj->getID()), now)
					|| obj->isKindOf(KINDOF_PROJECTILE)) continue;
				if (!obj->isKindOf(KINDOF_STRUCTURE) && !obj->isKindOf(KINDOF_VEHICLE)
					&& !obj->isKindOf(KINDOF_INFANTRY)) continue;
				const Real dx = obj->getPosition()->x - center.x;
				const Real dy = obj->getPosition()->y - center.y;
				if (!economicAssaultCanAttack(representatives, obj)) continue;
				// Structure pressure is the phase objective. Avoid committing every
				// independent cohort to one mobile enemy while bases remain intact.
				if (!obj->isKindOf(KINDOF_STRUCTURE) && commitments[static_cast<UnsignedInt>(obj->getID())] >= 24) continue;
				const Real score = (dx * dx + dy * dy) * (obj->isKindOf(KINDOF_STRUCTURE) ? 0.2f : 1.0f);
				if (score < bestScore || (score == bestScore && best != nullptr && obj->getID() < best->getID()))
				{
					best = obj;
					bestScore = score;
				}
			}
			if (best != nullptr)
			{
				targetId = static_cast<UnsignedInt>(best->getID());
				targetPosition = *best->getPosition();
				return true;
			}
			const EnemyMemoryItem* remembered = nullptr;
			for (const auto& item : m_autonomy.enemyMemory.getItems())
			{
				if (!item.isStructure) continue;
				if (economicAssaultTargetDeferred(item.objectId, now)) continue;
				Player* enemy = getPlayerByIndex(item.playerIndex);
				if (enemy == nullptr || (enemy->getDefaultTeam() == nullptr || player->getRelationship(enemy->getDefaultTeam()) != ENEMIES)) continue;
				const auto searched = m_autonomy.state.searchedAssaultObjectives.find(item.objectId);
				if (searched != m_autonomy.state.searchedAssaultObjectives.end() && searched->second >= item.lastSeenTick) continue;
				const Real dx = item.position.x - center.x, dy = item.position.y - center.y;
				const Real score = dx * dx + dy * dy;
				Coord3D pos = {item.position.x, item.position.y, item.position.z};
				if (score <= 320.0f * 320.0f && ThePartitionManager != nullptr
					&& ThePartitionManager->getShroudStatusForPlayer(player->getPlayerIndex(), &pos) == CELLSHROUD_CLEAR)
				{
					m_autonomy.state.searchedAssaultObjectives[item.objectId] = item.lastSeenTick;
					continue;
				}
				if (score < bestScore) { remembered = &item; bestScore = score; }
			}
			if (remembered == nullptr) return false;
			targetId = remembered->objectId;
			targetPosition = {remembered->position.x, remembered->position.y, remembered->position.z};
			return true;
		}

		void recordEconomicAssaultEvent(const CombatTask& task, const char* label, const char* reason)
		{
			appendRecordingEvent("attack", label, reason, task.taskId, task.targetObjectId,
				"autonomous_assault", "", "attacking", &task.targetPosition, true);
		}

		bool updateEconomicAssaultTask(Player* player, CombatTask& task)
		{
			if (player == nullptr || TheGameLogic == nullptr) return true;
			const DWORD now = adapterGameplayTick();
			if (!isAutonomyModeActive() || !areAutonomySimCommandsEnabled() || !m_automation.attackRule.enabled) return true;
			if (AIControlAdapterIsTickInFuture(task.assaultNextEvaluationTick, now)) return true;
			task.assaultNextEvaluationTick = now + 5000u;
			Coord3D center = {};
			std::vector<unsigned int> retained;
			std::vector<ObjectID> idleIds;
			for (unsigned int id : task.assignedUnitIds)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				if (unit == nullptr || unit->isEffectivelyDead() || unit->getControllingPlayer() != player
					|| unit->getContainedBy() != nullptr || unit->getPosition() == nullptr) continue;
				retained.push_back(id);
				center.x += unit->getPosition()->x; center.y += unit->getPosition()->y;
				if (unit->getAI() != nullptr && unit->getAI()->isIdle()) idleIds.push_back(static_cast<ObjectID>(id));
			}
			task.assignedUnitIds = retained;
			task.confirmedDeadCount = std::max(0, task.initialUnitCount - static_cast<int>(retained.size()));
			if (retained.empty()) { m_autonomy.combatTaskManager.failTask(task.taskId, "assault_no_survivors"); return true; }
			center.x /= static_cast<Real>(retained.size()); center.y /= static_cast<Real>(retained.size());
			UnsignedInt targetId = 0u; Coord3D targetPosition = {};
			Object* current = TheGameLogic->findObjectByID(static_cast<ObjectID>(task.targetObjectId));
			const bool currentVisible = current != nullptr && (current->getShroudedStatus(player->getPlayerIndex()) == OBJECTSHROUD_CLEAR
				|| current->getShroudedStatus(player->getPlayerIndex()) == OBJECTSHROUD_PARTIAL_CLEAR);
			const auto representatives = economicAssaultRepresentatives(retained);
			bool keepCurrent = currentVisible && !current->isEffectivelyDead() && isEnemyControlledObject(player, current)
				&& current->getPosition() != nullptr && !economicAssaultTargetDeferred(task.targetObjectId, now)
				&& economicAssaultCanAttack(representatives, current);
			if (keepCurrent)
			{
				const BodyModuleInterface* body = current->getBodyModule();
				const Real health = body != nullptr ? body->getHealth() : -1.0f;
				if (task.assaultTargetStartTick == 0u)
					task.assaultTargetStartTick = task.assaultLastDamageTick = now;
				if (health >= 0.0f && task.assaultTargetHealth >= 0.0f && health < task.assaultTargetHealth - 0.5f)
					task.assaultLastDamageTick = task.raidLastProgressTick = now;
				task.assaultTargetHealth = health;
				int otherCommitted = 0;
				for (const CombatTask* other : m_autonomy.combatTaskManager.findAttackTasks())
					if (other->taskId != task.taskId && other->owner == "autonomous_assault" && other->targetObjectId == task.targetObjectId)
						otherCommitted += static_cast<int>(other->assignedUnitIds.size());
				const bool stalled = now - task.raidLastProgressTick >= 45000u;
				const bool fruitlessPursuit = !current->isKindOf(KINDOF_STRUCTURE) && now - task.assaultLastDamageTick >= 90000u;
				if (stalled || fruitlessPursuit)
				{
					m_autonomy.state.deferredAssaultObjectives[task.targetObjectId] = now + 120000u;
					++task.raidRecoveryAttempts;
					recordEconomicAssaultEvent(task, "assault_objective_deferred", fruitlessPursuit ? "ineffective_mobile_pursuit" : "no_target_damage_or_group_progress");
					keepCurrent = false;
				}
				else if (!current->isKindOf(KINDOF_STRUCTURE) && otherCommitted >= 24) keepCurrent = false;
			}
			else if (!currentVisible && task.targetObjectId != 0u && task.raidLastProgressTick != 0u
				&& now - task.raidLastProgressTick >= 45000u)
			{
				m_autonomy.state.deferredAssaultObjectives[task.targetObjectId] = now + 120000u;
				++task.raidRecoveryAttempts;
				recordEconomicAssaultEvent(task, "assault_objective_deferred", "remembered_objective_no_group_progress");
			}
			if (keepCurrent) { targetId = task.targetObjectId; targetPosition = *current->getPosition(); }
			const bool found = keepCurrent || chooseEconomicAssaultTarget(player, center, targetId, targetPosition, retained, task.taskId);
			if (!found)
			{
				// Await fresh scouting at the front. Keep ownership so idle guard
				// cannot redirect survivors back to a zone between objectives.
				if (task.lastCommandTick != 0u && task.reason != "assault_awaiting_enemy_intelligence")
				{
					std::vector<ObjectID> holdIds;
					for (unsigned int id : retained) holdIds.push_back(static_cast<ObjectID>(id));
					std::string reason;
					executeScopedSelectionCommand(player, holdIds, reason, [&]() -> bool {
						return appendPlayerMessage(player, GameMessage::MSG_DO_STOP) != nullptr;
					});
					recordEconomicAssaultEvent(task, "assault_objective_wait", "no_viable_objective");
				}
				task.targetObjectId = 0u; task.assaultTargetTemplate.clear();
				task.assaultTargetStartTick = task.assaultLastDamageTick = 0u;
				task.assaultTargetHealth = -1.0f; task.lastCommandTick = 0u;
				task.raidLastProgressTick = now; task.raidBestQuorumDistance = -1.0f;
				task.reason = "assault_awaiting_enemy_intelligence";
				return true;
			}
			const bool retarget = targetId != task.targetObjectId;
			if (retarget)
			{
				task.raidLastProgressTick = now;
				task.raidBestQuorumDistance = -1.0f;
				task.assaultTargetStartTick = task.assaultLastDamageTick = now;
				task.assaultTargetHealth = -1.0f;
			}
			int arrived = 0, engaged = 0;
			Real bestDistance = 1.0e30f;
			std::vector<Real> distances;
			for (unsigned int id : retained)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				const Real dx = unit->getPosition()->x - targetPosition.x, dy = unit->getPosition()->y - targetPosition.y;
				const Real distance = std::sqrt(dx * dx + dy * dy);
				distances.push_back(distance);
				if (distance <= 320.0f) ++arrived;
				const Weapon* weapon = unit->getCurrentWeapon();
				if (weapon != nullptr && weapon->getLastShotFrame() > 0u
					&& TheGameLogic->getFrame() - weapon->getLastShotFrame() <= 8u * LOGICFRAMES_PER_SECOND) ++engaged;
			}
			std::sort(distances.begin(), distances.end());
			bestDistance = distances[distances.size() / 2u];
			task.arrivedCount = arrived; task.missingCount = static_cast<int>(retained.size()) - arrived;
			task.raidEngagedUnitCount = engaged; task.raidQuorumDistance = bestDistance;
			// Nearby incidental fire is not progress against this objective.
			if (task.raidBestQuorumDistance < 0.0f || bestDistance < task.raidBestQuorumDistance - 50.0f)
			{
				task.raidLastProgressTick = now; task.raidBestQuorumDistance = bestDistance;
			}
			// Fogged movement can receive a refresh before the next evaluation
			// defers an unreachable objective. Ownership remains at the front.
			const bool stalled = now - task.raidLastProgressTick >= 45000u;
			task.targetObjectId = targetId; task.targetPosition = targetPosition;
			task.waypoints = {{targetPosition, 320.0f}}; task.currentWaypointIndex = 0;
			Object* target = TheGameLogic->findObjectByID(static_cast<ObjectID>(targetId));
			const bool visible = target != nullptr && (target->getShroudedStatus(player->getPlayerIndex()) == OBJECTSHROUD_CLEAR
				|| target->getShroudedStatus(player->getPlayerIndex()) == OBJECTSHROUD_PARTIAL_CLEAR);
			const bool revealedObjective = visible && task.reason == "assault_remembered_position";
			task.assaultTargetTemplate = visible && target->getTemplate() != nullptr ? target->getTemplate()->getName().str() : "";
			std::vector<ObjectID> selected;
			for (unsigned int id : (retarget || revealedObjective || stalled || task.lastCommandTick == 0u ? retained : std::vector<unsigned int>(idleIds.begin(), idleIds.end())))
				selected.push_back(static_cast<ObjectID>(id));
			// Attack-move by default so cohorts fight whatever they meet on the way (a direct
			// attack order marches past defenders and gets picked off). Only units already
			// close to a visible objective get the direct order; a retarget leaves units that
			// are currently fighting alone unless the cohort has stalled.
			const bool reissueAll = task.lastCommandTick == 0u || stalled;
			std::vector<ObjectID> directIds, moveIds;
			for (ObjectID id : selected)
			{
				Object* unit = TheGameLogic->findObjectByID(id);
				if (unit == nullptr || unit->getPosition() == nullptr) continue;
				const Weapon* weapon = unit->getCurrentWeapon();
				const bool fighting = weapon != nullptr && weapon->getLastShotFrame() > 0u
					&& TheGameLogic->getFrame() - weapon->getLastShotFrame() <= 3u * LOGICFRAMES_PER_SECOND;
				if (fighting && !reissueAll) continue;
				const Real dx = unit->getPosition()->x - targetPosition.x, dy = unit->getPosition()->y - targetPosition.y;
				const bool nearObjective = dx * dx + dy * dy <= 260.0f * 260.0f;
				if (visible && nearObjective && TheActionManager != nullptr && TheActionManager->getCanAttackObject(unit, target,
					CMD_FROM_PLAYER, ATTACK_NEW_TARGET) >= ATTACKRESULT_POSSIBLE_AFTER_MOVING) directIds.push_back(id);
				else moveIds.push_back(id);
			}
			std::string reason;
			bool issued = false;
			if (!directIds.empty())
				issued = executeScopedSelectionCommand(player, directIds, reason, [&]() -> bool {
					GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_ATTACK_OBJECT);
					if (msg == nullptr) return false;
					msg->appendObjectIDArgument(target->getID());
					return true;
				});
			if (!moveIds.empty())
				issued = executeScopedSelectionCommand(player, moveIds, reason, [&]() -> bool {
					GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_ATTACKMOVETO);
					if (msg == nullptr) return false;
					msg->appendLocationArgument(targetPosition);
					return true;
				}) || issued;
			if (issued) task.lastCommandTick = now;
			task.state = CombatTaskState::Attacking;
			task.reason = visible ? "assault_visible_object" : "assault_remembered_position";
			if (issued && !directIds.empty())
				recordEconomicAssaultEvent(task, "assault_object_order", "visible_enemy_near");
			if (issued && !moveIds.empty())
				recordEconomicAssaultEvent(task, "assault_attack_move", visible ? "visible_objective" : "remembered_position");
			if (issued && stalled)
				recordEconomicAssaultEvent(task, "assault_forward_recovery", "no_recent_shots_or_progress");
			if (issued && retarget)
				recordEconomicAssaultEvent(task, "assault_retarget", task.reason.c_str());
			return true;
		}

		void evaluateEconomicAssault(DWORD now)
		{
			if (AIControlAdapterIsTickInFuture(m_autonomy.state.nextAssaultTick, now)) return;
			m_autonomy.state.nextAssaultTick = now + 2000u;
			if (!economicAssaultReady(now)) return;
			for (auto it = m_autonomy.state.deferredAssaultObjectives.begin(); it != m_autonomy.state.deferredAssaultObjectives.end(); )
				if (!AIControlAdapterIsTickInFuture(it->second, now)) it = m_autonomy.state.deferredAssaultObjectives.erase(it);
				else ++it;
			Player* player = resolveAutonomyPlayer();
			if (player == nullptr || TheGameLogic == nullptr) return;
			// Release quiet mobile defense allocations immediately once pressure
			// has cleared; preserve scout/capture/garrison/worker ownership.
			for (const auto& entry : m_autonomy.state.zoneDefenseAllocations)
				for (unsigned int taskId : entry.second.taskIds)
					m_autonomy.combatTaskManager.completeTask(taskId, "economic_victory_commit");
			m_autonomy.state.zoneDefenseAllocations.clear();
			for (CombatTask* task : m_autonomy.combatTaskManager.findActiveTasks())
			{
				if (task->type != CombatTaskType::Attack || task->owner != "autonomous_attack") continue;
				finishRaidProbe(*task, CombatTaskProbeState::Cancelled, "economic_victory_commit", now, 0.0f);
				task->owner = "autonomous_assault"; task->timeoutTick = 0u; task->manageTimeoutExternally = true;
				task->state = CombatTaskState::Attacking; task->lastCommandTick = 0u; task->assaultNextEvaluationTick = 0u;
				recordEconomicAssaultEvent(*task, "assault_commit", "existing_raid");
				updateEconomicAssaultTask(player, *task);
			}
			std::vector<Object*> units;
			collectCombatUnitsForRaid(player, units, false, "mixed_local", false);
			units.erase(std::remove_if(units.begin(), units.end(), [](Object* unit) {
				return unit == nullptr || unit->isEffectivelyDead() || unit->getContainedBy() != nullptr
					|| unit->getPosition() == nullptr || !unit->isAbleToAttack();
			}), units.end());
			// Leave free infantry for capture (Rebels) and garrisons (RPG troopers): the
			// assault otherwise recruits every unit and those systems never get any.
			{
				int rebelsToKeep = m_autonomy.state.captureInfantryReserve;
				int rpgToKeep = m_autonomy.state.garrisonInfantryReserve;
				units.erase(std::remove_if(units.begin(), units.end(), [&](Object* unit) {
					const ThingTemplate* templ = unit->getTemplate();
					if (templ == nullptr || !unit->isKindOf(KINDOF_INFANTRY)) return false;
					const std::string name = templ->getName().str();
					if (rpgToKeep > 0 && (containsIgnoreCase(name, "tunneldefender") || containsIgnoreCase(name, "rpg"))) { --rpgToKeep; return true; }
					if (rebelsToKeep > 0 && containsIgnoreCase(name, "rebel")) { --rebelsToKeep; return true; }
					return false;
				}), units.end());
			}
			if (units.empty()) return;
			// One bounded local cohort per pass avoids a single 1,000-unit
			// selection/formation; every free cohort gets its turn, including infantry.
			std::sort(units.begin(), units.end(), [](Object* a, Object* b) { return a->getID() < b->getID(); });
			const Coord3D seed = *units.front()->getPosition();
			UnsignedInt targetId = 0u; Coord3D targetPosition = {};
			std::vector<unsigned int> ids, scuds, escorts;
			for (Object* unit : units)
			{
				const Real dx = unit->getPosition()->x - seed.x, dy = unit->getPosition()->y - seed.y;
				if (dx * dx + dy * dy > 1000.0f * 1000.0f) continue;
				const ThingTemplate* templ = unit->getTemplate();
				if (templ != nullptr && containsIgnoreCase(templ->getName().str(), "scudlauncher"))
					scuds.push_back(static_cast<unsigned int>(unit->getID()));
				else escorts.push_back(static_cast<unsigned int>(unit->getID()));
			}
			// Give local siege ten seats before filling a 96-unit assault cohort.
			// Remaining launchers join later cohorts instead of being left behind.
			if (scuds.size() > 10u) scuds.resize(10u);
			ids = scuds;
			for (unsigned int id : escorts)
			{
				if (ids.size() >= 96u) break;
				ids.push_back(id);
			}
			if (!chooseEconomicAssaultTarget(player, seed, targetId, targetPosition, ids)) return;
			const auto taskId = m_autonomy.combatTaskManager.createTask(CombatTaskType::Attack, ids, targetPosition,
				"autonomous_assault", "economic_victory_commit", 0);
			CombatTask* task = m_autonomy.combatTaskManager.findTask(taskId);
			if (task == nullptr) return;
			task->manageTimeoutExternally = true; task->timeoutTick = 0u;
			task->raidMode = "mixed_local"; task->raidScudLauncherUnitIds = scuds;
			task->raidDesiredScudLaunchers = static_cast<int>(scuds.size());
			recordEconomicAssaultEvent(*task, "assault_commit", "available_local_cohort");
			updateEconomicAssaultTask(player, *task);
		}

		bool refillAutonomyProduction(Player* player, const ProductionManagerInputs& input,
			std::vector<ProductionProducerSnapshot> producers, std::string& reason,
			int orderLimit = 64, bool emergencyPulse = false,
			const std::string& utilityTemplate = "", Int utilityProducer = -1,
			int cap = 1000, const std::string& preferredCommand = "", int quadFloor = 0, int scorpionFloor = 0)
		{
			reconcilePendingUnitQueues();
			int pendingCount = 0;
			unsigned long long committed = 0u;
			for (const auto& pending : m_pendingUnitQueues)
				if (pending.playerIndex == player->getPlayerIndex())
				{
					committed += pending.cost;
					if (!containsIgnoreCase(pending.templateName, "worker") && !containsIgnoreCase(pending.templateName, "radarvan")
						&& !containsIgnoreCase(pending.templateName, "technical")) ++pendingCount;
				}
			const auto profilePlan = m_autonomy.state.profileSpendPlan;
			const bool mixedUtility = profilePlan.active && !utilityTemplate.empty();
			bool utilityIssued = false;
			int remaining = mixedUtility ? std::max(0, cap - input.armyCount - pendingCount) + 1
				: !utilityTemplate.empty() ? 1 : std::max(0, cap - input.armyCount - pendingCount);
			if (profilePlan.active && !profilePlan.sustainedOffense)
			{
				quadFloor = profilePlan.threatResponse ? std::max(12, quadFloor) : 6;
				scorpionFloor = profilePlan.threatResponse ? std::max(8, scorpionFloor) : 4;
			}
			int scuds = input.scudLaunchers + input.queuedScudLaunchers;
			for (const auto& pending : m_pendingUnitQueues)
				if (pending.playerIndex == player->getPlayerIndex() && containsIgnoreCase(pending.templateName, "scudlauncher")) ++scuds;
			int quads = input.quads + input.queuedQuads, scorpions = input.scorpions + input.queuedScorpions;
			int pendingInfantry = 0;
			for (const auto& pending : m_pendingUnitQueues)
				if (pending.playerIndex == player->getPlayerIndex())
				{
					if (containsIgnoreCase(pending.templateName, "quad")) ++quads;
					else if (containsIgnoreCase(pending.templateName, "scorpion")) ++scorpions;
					else if (containsIgnoreCase(pending.templateName, "rebel") || containsIgnoreCase(pending.templateName, "rpg")) ++pendingInfantry;
				}
			const int desiredScuds = input.hasCompletedPalace && input.hasScudLauncherScience
				? (input.economicVictory ? std::max(4, (quads + scorpions + scuds + 9) / 10)
					: std::min(6, std::max(2, (quads + scorpions + scuds + 9) / 10))) : 0;
			int infantry = input.soldiers + input.rpg;
			int queuedInfantry = 0;
			for (const auto& producer : producers)
			{
				if (!producer.isBarracks || producer.object == nullptr || producer.underConstruction) continue;
				auto* production = producer.object->getProductionUpdateInterface();
				if (production == nullptr) continue;
				for (const auto& name : {inferRpgTemplateForProducer(producer.object), inferSoldierTemplateForPlayer(player, producer.object)})
				{
					const auto* templ = name.empty() ? nullptr : TheThingFactory->findTemplate(AsciiString(name.c_str()), false);
					if (templ != nullptr) queuedInfantry += static_cast<int>(production->countUnitTypeInQueue(templ));
				}
			}
			infantry += queuedInfantry + pendingInfantry;
			// Keep one market and local-worker allowance ahead of optional refill.
			// A genuine emergency may spend a small pulse, retaining market money.
			const UnsignedInt protectedCash = profilePlan.active ? profilePlan.combatProtectedCash : emergencyPulse ? (input.money < 4000u ? 600u : 3000u)
				: input.reserveCash + (input.money >= input.reserveCash + 20000u ? 5000u : 3000u);
			int issued = 0; int siegeIssued = 0; int counterIssued = 0;
			std::sort(producers.begin(), producers.end(), [](const ProductionProducerSnapshot& a, const ProductionProducerSnapshot& b) {
				return a.objectId < b.objectId;
			});
			// Two rounds refill shallow queues fairly, at most 64 orders per pass.
			for (int round = 0; round < 2 && remaining > 0 && issued < orderLimit; ++round)
			{
				for (const auto& producer : producers)
				{
					if (remaining <= 0 || issued >= orderLimit) break;
					Object* obj = producer.object;
					if (obj == nullptr || producer.underConstruction || obj->isEffectivelyDead()) continue;
					ProductionUpdateInterface* production = obj->getProductionUpdateInterface();
					if (production == nullptr || (!producer.isBarracks && !producer.isWarFactoryLike)) continue;
					int depth = static_cast<int>(production->getProductionCount());
					for (const auto& pending : m_pendingUnitQueues) if (pending.producerId == obj->getID()) ++depth;
					if (depth >= 2) continue;
					std::string unit;
					const bool utilityOrder = !utilityTemplate.empty() && (!mixedUtility || (!utilityIssued && producer.objectId == utilityProducer));
					if (utilityOrder)
					{
						if (producer.objectId != utilityProducer || (!mixedUtility && issued > 0)) continue;
						unit = utilityTemplate;
					}
					else if (!autonomyRuleEnabled("combat_production"))
					{
						if (!producer.isWarFactoryLike || !autonomyRuleEnabled("siege_production") || scuds >= desiredScuds) continue;
						unit = AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(player);
					}
					else if (emergencyPulse)
					{
						if (!producer.isWarFactoryLike) continue;
						unit = preferredCommand == "Game.QueueScorpionsAllWarFactories"
							? inferScorpionTemplateForProducer(obj) : inferQuadTemplateForProducer(obj);
					}
					else if (producer.isBarracks)
					{
						if (autonomyRuleEnabled("siege_production") && profilePlan.active && scuds < std::min(2, desiredScuds)) continue;
						const int infantryFloor = profilePlan.active && !profilePlan.sustainedOffense ? (profilePlan.threatResponse ? 8 : 4) : input.economicVictory ? 20 : 12;
						if (infantry >= std::max(infantryFloor, input.armyCount / 5)) continue;
						unit = preferredCommand == "Game.QueueSoldiersAllBarracks"
							? inferSoldierTemplateForPlayer(player, obj) : inferRpgTemplateForProducer(obj);
					}
					else if (quads < quadFloor && (counterIssued == 0 || scuds >= desiredScuds || quads < 6))
						unit = inferQuadTemplateForProducer(obj);
					else if (scorpions < scorpionFloor && (counterIssued == 0 || scuds >= desiredScuds || scorpions < 6))
						unit = inferScorpionTemplateForProducer(obj);
					else if (autonomyRuleEnabled("siege_production") && scuds < desiredScuds)
						unit = AIControlAdapterTemplateInferenceService::InferScudLauncherTemplate(player);
					else if (quads < scorpions * 4 / 3) unit = inferQuadTemplateForProducer(obj);
					else if ((issued + producer.objectId) % 5 == 0 && input.hasCompletedPalace) unit = inferRocketBuggyTemplateForProducer(obj);
					else unit = inferScorpionTemplateForProducer(obj);
					const ThingTemplate* templ = unit.empty() ? nullptr : TheThingFactory->findTemplate(AsciiString(unit.c_str()), false);
					if (templ == nullptr || TheBuildAssistant->canMakeUnit(obj, templ) != CANMAKE_OK)
					{
						// A dealer lacking the siege capability still feeds escorts.
						if (producer.isBarracks || utilityOrder) continue;
						unit = inferQuadTemplateForProducer(obj);
						templ = unit.empty() ? nullptr : TheThingFactory->findTemplate(AsciiString(unit.c_str()), false);
						if (templ == nullptr || TheBuildAssistant->canMakeUnit(obj, templ) != CANMAKE_OK) continue;
					}
					const UnsignedInt cost = static_cast<UnsignedInt>(std::max<Int>(0, templ->calcCostToBuild(player)));
					const Money* wallet = player->getMoney();
					if (wallet == nullptr || committed + cost + protectedCash > wallet->countMoney()) continue;
					if (!queueUnit(player, obj, unit, 1, producer.isBarracks ? "barracks" : "arms_dealer", reason, utilityOrder && producer.isBarracks ? "capture" : "")) continue;
					if (utilityOrder) utilityIssued = true;
					committed += cost; --remaining; ++issued;
					if (producer.isBarracks) ++infantry;
					if (containsIgnoreCase(unit, "scudlauncher")) { ++scuds; ++siegeIssued; }
					else if (containsIgnoreCase(unit, "quad")) { ++quads; ++counterIssued; }
					else if (containsIgnoreCase(unit, "scorpion")) { ++scorpions; ++counterIssued; }
				}
			}
			m_autonomy.state.productionTelemetry["refill_issued"] = issued;
			m_autonomy.state.productionTelemetry["refill_queue_limit"] = 2;
			m_autonomy.state.productionTelemetry["refill_siege_issued"] = siegeIssued;
			m_autonomy.state.productionTelemetry["refill_phase"] = input.economicVictory ? "economic_victory" : "pre_milestone";
			m_autonomy.state.productionTelemetry["refill_desired_scuds"] = desiredScuds;
			m_autonomy.state.productionTelemetry["refill_protected_cash"] = protectedCash;
			m_autonomy.state.productionTelemetry["refill_order_limit"] = orderLimit;
			m_autonomy.state.productionTelemetry["refill_emergency_pulse"] = emergencyPulse;
			m_autonomy.state.productionTelemetry["refill_utility_template"] = utilityTemplate;
			reason = issued > 0 ? (input.economicVictory ? "economic_victory_refill" : "adaptive_shallow_refill") : remaining <= 0 ? "army_cap_reached" : "refill_queues_or_budget_satisfied";
			return issued > 0;
		}
