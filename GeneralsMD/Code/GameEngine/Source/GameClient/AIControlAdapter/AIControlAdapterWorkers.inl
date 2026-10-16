		struct WorkerSearchContext
		{
			AIControlAdapterState* self;
			std::vector<Object*> availableIdleDozers;
			std::vector<Object*> availableFallbackDozers;
			Object* firstDozer;
		};

		void pruneExpiredWorkerReservations()
		{
			if (m_workers.reservedWorkersUntilTick.empty())
			{
				return;
			}
			const DWORD now = adapterGameplayTick();
			for (auto it = m_workers.reservedWorkersUntilTick.begin(); it != m_workers.reservedWorkersUntilTick.end(); )
			{
				const DWORD untilTick = it->second;
				// Handles tick wrap correctly with signed subtraction.
				if (static_cast<LONG>(untilTick - now) <= 0)
				{
					it = m_workers.reservedWorkersUntilTick.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		bool isWorkerTemporarilyReserved(const Object* worker)
		{
			if (worker == nullptr)
			{
				return false;
			}
			pruneExpiredWorkerReservations();
			const ObjectID id = worker->getID();
			if (static_cast<Int>(id) <= 0)
			{
				return false;
			}
			const auto it = m_workers.reservedWorkersUntilTick.find(id);
			if (it == m_workers.reservedWorkersUntilTick.end())
			{
				return false;
			}
			const DWORD now = adapterGameplayTick();
			return static_cast<LONG>(it->second - now) > 0;
		}

		void reserveWorkerForBuild(Object* worker, DWORD durationMs = 5000u)
		{
			if (worker == nullptr)
			{
				return;
			}
			const ObjectID id = worker->getID();
			if (static_cast<Int>(id) <= 0)
			{
				return;
			}
			m_workers.reservedWorkersUntilTick[id] = adapterGameplayTick() + durationMs;
		}

		bool moveAutonomyUnits(Player* player, const std::vector<unsigned int>& unitIds,
			const Coord3D& target, std::string& reason)
		{
			if (player == nullptr || TheGameLogic == nullptr) { reason = "player_not_found"; return false; }
			std::vector<ObjectID> selected;
			for (unsigned int id : unitIds)
			{
				Object* unit = TheGameLogic->findObjectByID(static_cast<ObjectID>(id));
				if (unit != nullptr && !unit->isEffectivelyDead() && unit->getControllingPlayer() == player &&
					unit->getAI() != nullptr && unit->getContainedBy() == nullptr) selected.push_back(unit->getID());
			}
			if (selected.empty()) { reason = "no_valid_objects"; return false; }
			return executeScopedSelectionCommand(player, selected, reason, [&]() -> bool
			{
				GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_MOVETO);
				if (msg == nullptr) { reason = "message_stream_not_ready"; return false; }
				msg->appendLocationArgument(target);
				return true;
			});
		}

		bool hasStrategicBuildNearZone(Real x, Real y)
		{
			const Real radius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
			for (SpecialTaskReservation* task : m_autonomy.taskReservationManager.findBuildTasks())
			{
				if (task == nullptr || !isPhase79StrategicFoundationTemplate(task->expectedTemplate)) continue;
				if (task->targetObjectId != 0u)
				{
					Object* foundation = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->targetObjectId));
					if (foundation == nullptr || foundation->isEffectivelyDead() ||
						foundation->getControllingPlayer() != resolveAutonomyPlayer() ||
						!foundation->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION)) continue;
				}
				const Real dx = task->targetPosition.x - x;
				const Real dy = task->targetPosition.y - y;
				if (dx * dx + dy * dy <= radius * radius) return true;
			}
			return false;
		}

		bool evacuateExposedBuilders(Player* player, DWORD now)
		{
			if (!isSurvivalRecoveryActive(now) || !areAutonomySimCommandsEnabled()) return false;
			Coord3D rear = {}; bool foundRear = false;
			for (const auto& zone : m_autonomy.state.telemetryZones)
				if (zone.value("anchor_id", 0u) == m_autonomy.state.recoveryZoneAnchor)
				{
					rear = {zone.value("rear_point_x", zone.value("center_x", 0.0f)),
						zone.value("rear_point_y", zone.value("center_y", 0.0f)), 0.0f}; foundRear = true; break;
				}
			if (!foundRear) return false;
			for (auto* task : m_autonomy.taskReservationManager.findBuildTasks())
			{
				// Redirect travel to exposed expansion sites; existing active foundations keep their builder.
				if (task == nullptr || task->targetObjectId != 0u || task->sourceObjectId == 0u
					|| (task->state != SpecialTaskState::Assigned && task->state != SpecialTaskState::Moving)) continue;
				if (!containsIgnoreCase(task->expectedTemplate, "supplystash") && !containsIgnoreCase(task->expectedTemplate, "tunnel")
					&& !containsIgnoreCase(task->expectedTemplate, "stinger")) continue;
				UnsignedInt nearest = 0u; Real best = 1.0e30f;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					const Real dx = task->targetPosition.x - zone.value("center_x", 0.0f), dy = task->targetPosition.y - zone.value("center_y", 0.0f);
					if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; nearest = zone.value("anchor_id", 0u); }
				}
				if (!isConsolidatedSurvivalZone(nearest, now)) continue;
				const auto threat = m_autonomy.state.zoneThreats.find(nearest);
				if (threat == m_autonomy.state.zoneThreats.end() || now - threat->second.lastSeenTick > 15000u
					|| threat->second.localEnemyCount <= 0) continue;
				Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->sourceObjectId));
				if (worker == nullptr || worker->isEffectivelyDead() || worker->getControllingPlayer() != player) continue;
				const auto workerId = task->sourceObjectId;
				m_autonomy.taskReservationManager.failTask(task->taskId, "survival_defer_exposed_expansion");
				m_workers.reservedWorkersUntilTick.erase(static_cast<ObjectID>(workerId));
				std::string reason;
				if (!moveAutonomyUnits(player, {workerId}, rear, reason)) return false;
				AutonomyWorkerTransfer transfer;
				transfer.zoneId = m_autonomy.state.recoveryZoneAnchor; transfer.x = rear.x; transfer.y = rear.y; transfer.expiresTick = now + 90000u;
				m_autonomy.state.workerTransfers[workerId] = transfer;
				reserveWorkerForBuild(worker, 90000u);
				appendRecordingEvent("worker", "worker_survival_return", "exposed_expansion_deferred", task->taskId, workerId);
				return true;
			}
			return false;
		}

		bool queueSurvivalWorkers(Player* player, const std::vector<Object*>& producers,
			const std::vector<Object*>& workers, DWORD now)
		{
			if (!isSurvivalRecoveryActive(now) || !areAutonomySimCommandsEnabled() || workers.size() >= 80u) return false;
			reconcilePendingUnitQueues();
			int queuedWorkers = 0;
			for (const auto& pending : m_pendingUnitQueues)
				if (pending.playerIndex == player->getPlayerIndex() && containsIgnoreCase(pending.templateName, "worker")) ++queuedWorkers;
			Object* best = nullptr; std::string bestTemplate; int bestDepth = 999;
			for (Object* producer : producers)
			{
				if (producer == nullptr || producer->isEffectivelyDead() || producer->getPosition() == nullptr) continue;
				const auto name = AIControlAdapterTemplateInferenceService::InferUnitTemplateForProducer(producer, "worker");
				const auto* templ = name.empty() ? nullptr : TheThingFactory->findTemplate(AsciiString(name.c_str()), false);
				auto* queue = producer->getProductionUpdateInterface();
				if (templ == nullptr || queue == nullptr) continue;
				queuedWorkers += static_cast<int>(queue->countUnitTypeInQueue(templ));
				bool inCore = false; bool dangerous = false;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					const Real dx = producer->getPosition()->x - zone.value("center_x", 0.0f);
					const Real dy = producer->getPosition()->y - zone.value("center_y", 0.0f);
					if (dx * dx + dy * dy > m_autonomy.state.zoneRadius * m_autonomy.state.zoneRadius) continue;
					const auto anchor = zone.value("anchor_id", 0u);
					inCore = inCore || m_autonomy.state.survivalCoreZones.count(anchor) != 0u;
					const auto threat = m_autonomy.state.zoneThreats.find(anchor);
					if (threat != m_autonomy.state.zoneThreats.end() && now - threat->second.lastSeenTick <= 10000u
						&& threat->second.localEnemyCount > 0) dangerous = true;
				}
				if (!inCore || (dangerous && workers.size() > 2u)) continue;
				int depth = static_cast<int>(queue->getProductionCount());
				for (const auto& pending : m_pendingUnitQueues) if (pending.producerId == producer->getID()) ++depth;
				if (depth < 2 && depth < bestDepth && TheBuildAssistant->canMakeUnit(producer, templ) == CANMAKE_OK)
				{ best = producer; bestTemplate = name; bestDepth = depth; }
			}
			int safeBuilders = 0;
			for (Object* worker : workers)
			{
				if (worker == nullptr || worker->getPosition() == nullptr || !isWorkerAvailableForNewBuild(worker)) continue;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (m_autonomy.state.survivalCoreZones.count(zone.value("anchor_id", 0u)) == 0u) continue;
					const Real dx = worker->getPosition()->x - zone.value("center_x", 0.0f);
					const Real dy = worker->getPosition()->y - zone.value("center_y", 0.0f);
					if (dx * dx + dy * dy <= m_autonomy.state.zoneRadius * m_autonomy.state.zoneRadius) { ++safeBuilders; break; }
				}
			}
			const int targetWorkers = std::max(8, static_cast<int>(m_autonomy.state.survivalCoreZones.size()) * 4);
			const int needed = std::max(targetWorkers - static_cast<int>(workers.size()), 4 - safeBuilders) - queuedWorkers;
			m_autonomy.state.workerLiquidityTelemetry["recovery_active"] = true;
			m_autonomy.state.workerLiquidityTelemetry["recovery_worker_target"] = targetWorkers;
			m_autonomy.state.workerLiquidityTelemetry["recovery_safe_builders"] = safeBuilders;
			m_autonomy.state.workerLiquidityTelemetry["recovery_queued_workers"] = queuedWorkers;
			if (best == nullptr || needed <= 0) return false;
			const auto* templ = TheThingFactory->findTemplate(AsciiString(bestTemplate.c_str()), false);
			const UnsignedInt cost = static_cast<UnsignedInt>(std::max<Int>(0, templ->calcCostToBuild(player)));
			AIControlAdapterStrategicSpendSnapshot spend;
			spend.money = availableAutonomyCash(player); spend.reserveCash = resolveAutonomyProfilePolicyConfig().reserveCash;
			spend.emergencySurvivalActive = true;
			const auto gate = AIControlAdapterStrategicSpendManager().Evaluate(spend, StrategicSpendCategory::LocalWorkerRecovery, cost);
			std::string reason;
			const bool issued = gate.allowed && queueUnit(player, best, bestTemplate, 1, "any", reason);
			m_autonomy.state.workerLiquidityTelemetry["recovery_issued"] = issued ? 1 : 0;
			m_autonomy.state.workerLiquidityTelemetry["reason"] = issued ? "safe_worker_recovery" : gate.allowed ? reason : gate.reason;
			return issued;
		}

		bool redistributeIdleWorkers(Player* player, const std::vector<Object*>& workers, DWORD now)
		{
			const auto& zones = m_autonomy.state.telemetryZones;
			const Real radius = std::max<Real>(160.0f, m_autonomy.state.zoneRadius);
			const Real radiusSq = radius * radius;
			for (auto it = m_autonomy.state.workerTransfers.begin(); it != m_autonomy.state.workerTransfers.end();)
			{
				Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(it->first));
				const Coord3D* pos = worker != nullptr ? worker->getPosition() : nullptr;
				const Real dx = pos != nullptr ? pos->x - it->second.x : radius;
				const Real dy = pos != nullptr ? pos->y - it->second.y : radius;
				if (worker == nullptr || worker->isEffectivelyDead() || worker->getControllingPlayer() != player ||
					!AIControlAdapterIsTickInFuture(it->second.expiresTick, now) || dx * dx + dy * dy < radiusSq * 0.25f)
				{
					const bool arrived = worker != nullptr && !worker->isEffectivelyDead() && worker->getControllingPlayer() == player && dx * dx + dy * dy < radiusSq * 0.25f;
					appendRecordingEvent("worker", arrived ? "worker_transfer_arrived" : "worker_transfer_released", arrived ? "arrival" : "lost_worker_or_expired", 0, it->first);
					m_workers.reservedWorkersUntilTick.erase(static_cast<ObjectID>(it->first));
					it = m_autonomy.state.workerTransfers.erase(it);
				}
				else ++it;
			}
			if (!autonomyRuleEnabled("worker_redistribution")) return false;
			std::vector<Object*> available;
			for (Object* worker : workers)
				if (worker->getContainedBy() == nullptr && isWorkerAvailableForNewBuild(worker)) available.push_back(worker);
			m_autonomy.state.workerLiquidityTelemetry = nlohmann::json::object({
				{"available_idle_workers", static_cast<int>(available.size())},
				{"transfers_in_progress", static_cast<int>(m_autonomy.state.workerTransfers.size())},
				{"reason", "no_transfer_needed"}
			});
			if (available.empty()) return false;

			// A worker belongs to its nearest zone, so overlapping circles cannot donate it twice.
			auto nearestZone = [&](Object* worker) -> UnsignedInt
			{
				const Coord3D* pos = worker->getPosition();
				Real best = radiusSq;
				UnsignedInt result = 0u;
				if (pos == nullptr) return result;
				for (const auto& zone : zones)
				{
					const Real dx = pos->x - zone.value("center_x", 0.0f);
					const Real dy = pos->y - zone.value("center_y", 0.0f);
					if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; result = zone.value("anchor_id", 0u); }
				}
				return result;
			};
			std::map<UnsignedInt, int> idleByZone;
			for (Object* worker : available) ++idleByZone[nearestZone(worker)];
			for (const auto& transfer : m_autonomy.state.workerTransfers) ++idleByZone[transfer.second.zoneId];
			for (const auto& zone : zones)
			{
				const UnsignedInt zoneId = zone.value("anchor_id", 0u);
				if (isConsolidatedSurvivalZone(zoneId, now)) continue;
				const bool main = zone.value("is_main_base", false);
				Coord3D target = {zone.value("center_x", 0.0f), zone.value("center_y", 0.0f), 0.0f};
				const int desired = isSurvivalRecoveryActive(now) ? 4 : (main || hasStrategicBuildNearZone(target.x, target.y)) ? 3 :
					(zone.value("active", false) ? 2 : (zone.value("developed", false) ? 1 : 0));
				int localIdle = 0;
				for (Object* worker : available)
				{
					const Coord3D* pos = worker->getPosition();
					if (pos == nullptr) continue;
					const Real dx = pos->x - target.x;
					const Real dy = pos->y - target.y;
					if (dx * dx + dy * dy < radiusSq) ++localIdle;
				}
				for (const auto& transfer : m_autonomy.state.workerTransfers)
					if (transfer.second.zoneId == zoneId) ++localIdle;
				const auto threat = m_autonomy.state.zoneThreats.find(zoneId);
				const bool dangerous = threat != m_autonomy.state.zoneThreats.end() && now - threat->second.lastSeenTick <= 10000u
					&& threat->second.localEnemyCount > 0;
				if (desired <= localIdle || dangerous) continue;
				Object* bestWorker = nullptr;
				Real bestDistSq = radiusSq * 36.0f;
				for (Object* worker : available)
				{
					const UnsignedInt donor = nearestZone(worker);
					if (donor == zoneId) continue;
					int donorFloor = 0;
					for (const auto& donorZone : zones)
						if (donorZone.value("anchor_id", 0u) == donor)
							donorFloor = (donorZone.value("is_main_base", false) ||
								hasStrategicBuildNearZone(donorZone.value("center_x", 0.0f), donorZone.value("center_y", 0.0f))) ? 3 :
								(donorZone.value("active", false) ? 2 : 1);
					if (isConsolidatedSurvivalZone(donor, now)) donorFloor = 0;
					else if (isSurvivalRecoveryActive(now)) donorFloor = 4;
					if (idleByZone[donor] <= donorFloor) continue;
					const Coord3D* pos = worker->getPosition();
					if (pos == nullptr) continue;
					const Real dx = pos->x - target.x;
					const Real dy = pos->y - target.y;
					if (dx * dx + dy * dy < bestDistSq) { bestDistSq = dx * dx + dy * dy; bestWorker = worker; }
				}
				if (bestWorker == nullptr) continue;
				std::string reason;
				if (!moveAutonomyUnits(player, {static_cast<unsigned int>(bestWorker->getID())}, target, reason)) continue;
				const DWORD duration = static_cast<DWORD>(std::min<Real>(180000.0f, std::max<Real>(30000.0f, std::sqrt(bestDistSq) * 75.0f)));
				AutonomyWorkerTransfer transfer;
				transfer.zoneId = zoneId; transfer.x = target.x; transfer.y = target.y; transfer.expiresTick = now + duration;
				m_autonomy.state.workerTransfers[static_cast<UnsignedInt>(bestWorker->getID())] = transfer;
				reserveWorkerForBuild(bestWorker, duration);
				m_autonomy.state.workerLiquidityTelemetry["reason"] = "idle_worker_redistributed";
				m_autonomy.state.workerLiquidityTelemetry["worker_id"] = static_cast<UnsignedInt>(bestWorker->getID());
				m_autonomy.state.workerLiquidityTelemetry["target_zone"] = zoneId;
				m_autonomy.state.workerLiquidityTelemetry["transfers_in_progress"] = static_cast<int>(m_autonomy.state.workerTransfers.size());
				recordAutonomyTelemetryEvent("worker", "redistribute_idle_worker", "frontier_liquidity", &target, 0, static_cast<unsigned int>(bestWorker->getID()));
				return true;
			}
			return false;
		}

		bool isWorkerAssignedToActiveConstruction(Object* worker) const
		{
			if (worker == nullptr || TheGameLogic == nullptr)
			{
				return false;
			}

			const ObjectID workerId = worker->getID();
			if (static_cast<Int>(workerId) <= 0)
			{
				return false;
			}

			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj->isEffectivelyDead())
				{
					continue;
				}
				if (!obj->isKindOf(KINDOF_STRUCTURE))
				{
					continue;
				}
				if (!obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					continue;
				}
				if (obj->getBuilderID() != workerId)
				{
					continue;
				}

				return true;
			}

			return false;
		}

		bool isWorkerAvailableForNewBuild(Object* worker, bool checkReservation = true)
		{
			if (worker == nullptr || worker->isEffectivelyDead())
			{
				return false;
			}
			if (!worker->isKindOf(KINDOF_DOZER))
			{
				return false;
			}
			if (worker->getContainedBy() != nullptr) return false;
			if (checkReservation)
			{
				if (m_autonomy.combatTaskManager.isUnitReserved(worker->getID())) return false;
				for (const CombatTask* task : m_autonomy.combatTaskManager.findActiveTasks())
					if (task != nullptr && std::find(task->scoutShuttlePassengerIds.begin(), task->scoutShuttlePassengerIds.end(),
						static_cast<unsigned int>(worker->getID())) != task->scoutShuttlePassengerIds.end()) return false;
			}
			if (worker->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
			{
				return false;
			}
			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr || !ai->isIdle() || ai->isBusy())
			{
				return false;
			}
			if (isWorkerForcedToSupply(worker))
			{
				return false;
			}
			// Extra guard: if this worker is currently assigned as builder for an
			// under-construction structure, treat it as not available even if AI
			// idle/busy briefly reports an inconsistent value.
			if (isWorkerAssignedToActiveConstruction(worker))
			{
				return false;
			}
			if (checkReservation && isWorkerTemporarilyReserved(worker))
			{
				return false;
			}
			// CRITICAL: Check task reservation system to prevent stealing workers from active build tasks
			if (checkReservation && m_autonomy.taskReservationManager.isObjectReserved(worker->getID()))
			{
				return false;
			}
			return true;
		}

		// A GLA worker forced into the supply "wanting" state (e.g. freshly produced) returns to
		// supply-truck mode right after a construct order and never leaves it, holding the build.
		static bool isWorkerForcedToSupply(const Object* worker)
		{
			const AIUpdateInterface* ai = worker != nullptr ? worker->getAI() : nullptr;
			const SupplyTruckAIInterface* supply = ai != nullptr ? ai->getSupplyTruckAIInterface() : nullptr;
			return supply != nullptr && supply->isForcedIntoWantingState();
		}

		bool isWorkerFallbackAvailableForNewBuild(Object* worker, bool checkReservation = true)
		{
			if (worker == nullptr || worker->isEffectivelyDead())
			{
				return false;
			}
			if (isWorkerForcedToSupply(worker))
			{
				return false;
			}
			if (!worker->isKindOf(KINDOF_DOZER))
			{
				return false;
			}
			if (worker->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
			{
				return false;
			}
			if (isWorkerAssignedToActiveConstruction(worker))
			{
				return false;
			}
			if (checkReservation && isWorkerTemporarilyReserved(worker))
			{
				return false;
			}
			// CRITICAL: Check task reservation system to prevent stealing workers from active build tasks
			if (checkReservation && m_autonomy.taskReservationManager.isObjectReserved(worker->getID()))
			{
				return false;
			}
			return true;
		}

		static void findWorkerCallback(Object* obj, void* userData)
		{
			if (obj == nullptr || userData == nullptr)
			{
				return;
			}
			if (obj->isEffectivelyDead())
			{
				return;
			}
			if (!obj->isKindOf(KINDOF_DOZER))
			{
				return;
			}

			WorkerSearchContext* ctx = static_cast<WorkerSearchContext*>(userData);
			if (ctx->firstDozer == nullptr)
			{
				ctx->firstDozer = obj;
			}
			if (ctx->self != nullptr && ctx->self->isWorkerAvailableForNewBuild(obj))
			{
				ctx->availableIdleDozers.push_back(obj);
				return;
			}
			if (ctx->self != nullptr && ctx->self->isWorkerFallbackAvailableForNewBuild(obj))
			{
				ctx->availableFallbackDozers.push_back(obj);
			}
		}

		Object* resolveWorkerFromArgs(Player* player, const nlohmann::json& message, bool requireIdle, std::string& reason)
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
				const auto workerIdIt = argsIt->find("worker_object_id");
				if (workerIdIt != argsIt->end() && workerIdIt->is_number_integer())
				{
					const Int workerId = workerIdIt->get<Int>();
					if (workerId <= 0)
					{
						reason = "invalid_worker_object_id";
						return nullptr;
					}

					Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(workerId));
					if (worker == nullptr)
					{
						reason = "worker_not_found";
						return nullptr;
					}
					if (worker->getControllingPlayer() != player)
					{
						reason = "worker_not_owned";
						return nullptr;
					}
					if (!worker->isKindOf(KINDOF_DOZER))
					{
						reason = "worker_not_dozer";
						return nullptr;
					}
					if (requireIdle)
					{
						bool allowReservedWorker = false;
						const auto allowReservedIt = argsIt->find("allow_reserved_worker");
						if (allowReservedIt != argsIt->end() && allowReservedIt->is_boolean())
						{
							allowReservedWorker = allowReservedIt->get<bool>();
						}

						// Explicit worker selections honor temporary reservation by default.
						// Internal multi-step flows can set allow_reserved_worker=true when they
						// intentionally pass the same worker id across a chained request.
						if (!isWorkerAvailableForNewBuild(worker, !allowReservedWorker)
							&& !isWorkerFallbackAvailableForNewBuild(worker, !allowReservedWorker))
						{
							reason = "worker_not_idle";
							return nullptr;
						}
					}
					return worker;
				}
			}

			WorkerSearchContext ctx = { this, std::vector<Object*>(), std::vector<Object*>(), nullptr };
			player->iterateObjects(findWorkerCallback, &ctx);
			// A GLA worker ferrying supplies can keep a construct order pending forever
			// (it waits for its supply logic to go idle), so try non-ferrying workers first.
			const auto ferryingBoundary = std::stable_partition(ctx.availableFallbackDozers.begin(), ctx.availableFallbackDozers.end(), [](Object* worker)
			{
				const AIUpdateInterface* ai = worker != nullptr ? worker->getAI() : nullptr;
				const SupplyTruckAIInterface* supply = ai != nullptr ? ai->getSupplyTruckAIInterface() : nullptr;
				return supply == nullptr || !supply->isCurrentlyFerryingSupplies();
			});
			if (ferryingBoundary != ctx.availableFallbackDozers.begin())
			{
				ctx.availableFallbackDozers.erase(ferryingBoundary, ctx.availableFallbackDozers.end());
			}
			if (requireIdle)
			{
				std::vector<Object*>* candidates = &ctx.availableIdleDozers;
				if (candidates->empty() && !ctx.availableFallbackDozers.empty())
				{
					candidates = &ctx.availableFallbackDozers;
				}
				if (candidates->empty())
				{
					reason = "idle_worker_not_found";
					return nullptr;
				}

				Object* selected = candidates->front();
				const Int playerIndex = player->getPlayerIndex();
				const auto lastIt = m_workers.lastSelectedWorkerByPlayer.find(playerIndex);
				if (lastIt != m_workers.lastSelectedWorkerByPlayer.end() && candidates->size() > 1u)
				{
					const ObjectID lastId = lastIt->second;
					for (std::size_t i = 0; i < candidates->size(); ++i)
					{
						if ((*candidates)[i] == nullptr || (*candidates)[i]->getID() != lastId)
						{
							continue;
						}
						const std::size_t nextIndex = (i + 1u) % candidates->size();
						selected = (*candidates)[nextIndex];
						break;
					}
				}

				if (selected != nullptr)
				{
					m_workers.lastSelectedWorkerByPlayer[playerIndex] = selected->getID();
					// Reserve the worker briefly so rapid-fire smart build requests don't
					// keep reselecting and interrupting the same unit.
					reserveWorkerForBuild(selected);
				}
				return selected;
			}
			// Even when idleness is not required, prefer an idle dozer to avoid
			// interrupting active construction/collection tasks.
			if (!ctx.availableIdleDozers.empty())
			{
				return ctx.availableIdleDozers.front();
			}
			if (!ctx.availableFallbackDozers.empty())
			{
				return ctx.availableFallbackDozers.front();
			}
			if (ctx.firstDozer == nullptr)
			{
				reason = "worker_not_found";
				return nullptr;
			}
			return ctx.firstDozer;
		}
