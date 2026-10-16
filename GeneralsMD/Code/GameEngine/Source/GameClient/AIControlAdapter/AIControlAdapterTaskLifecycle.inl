/**
 * AIControlAdapterTaskLifecycle.inl
 *
 * Phase 6.1: Construction task lifecycle tracking and abandoned-foundation recovery.
 *
 * This system ensures that build tasks remain owned until the structure is:
 * - Complete (finished construction)
 * - Recovered (reassigned to another worker)
 * - Failed (worker dead, foundation invalid)
 * - Expired (timeout)
 *
 * Core invariant:
 * owned under_construction strategic structure => active build reservation exists
 *
 * Key fixes (2026-05-27):
 * - Use targetObjectId for foundation lookup once set (more reliable than position scan)
 * - Adopt orphan foundations (under-construction strategic structures with no active task)
 * - Only fail tasks when foundation is truly gone (verified by object ID)
 * - Worker loss does not drop the task if foundation exists
 */

bool isStrategicStructureTemplate(const std::string& templateName)
{
	// Strategic GLA structures that should always have active build reservations while under construction
	static const char* STRATEGIC_STRUCTURE_TEMPLATES[] = {
		"GLASupplyStash",
		"GLAArmsDealer",
		"GLABarracks",
		"GLATunnelNetwork",
		"GLAStingerSite",
		"GLAScudStorm",
		"GLAPalace",
		"GLABlackMarket",
		"GLACommandCenter",
		nullptr
	};

	for (int i = 0; STRATEGIC_STRUCTURE_TEMPLATES[i] != nullptr; ++i)
	{
		if (templateName == STRATEGIC_STRUCTURE_TEMPLATES[i])
		{
			return true;
		}
	}
	return false;
}

bool isPhase79StrategicFoundationTemplate(const std::string& templateName)
{
	return containsIgnoreCase(templateName, "scudstorm") ||
		containsIgnoreCase(templateName, "palace");
}

Real getObjectHealthForStrategicFoundation(Object* obj)
{
	if (obj == nullptr)
	{
		return -1.0f;
	}
	BodyModuleInterface* body = obj->getBodyModule();
	return body != nullptr ? body->getHealth() : -1.0f;
}

Real getObjectMaxHealthForStrategicFoundation(Object* obj)
{
	if (obj == nullptr)
	{
		return -1.0f;
	}
	BodyModuleInterface* body = obj->getBodyModule();
	return body != nullptr ? body->getMaxHealth() : -1.0f;
}

// True while this worker has a pending build order on the foundation and is not held in
// GLA supply-truck mode (which can leave a construct order pending indefinitely).
static bool isWorkerBuildingFoundation(Object* worker, ObjectID foundationId)
{
	if (worker == nullptr || worker->isEffectivelyDead() || !worker->isKindOf(KINDOF_DOZER) || worker->getAI() == nullptr)
	{
		return false;
	}
	DozerAIInterface* dozer = worker->getAI()->getDozerAIInterface();
	if (dozer == nullptr || !dozer->isTaskPending(DOZER_TASK_BUILD) || dozer->getTaskTarget(DOZER_TASK_BUILD) != foundationId)
	{
		return false;
	}
	const SupplyTruckAIInterface* supply = worker->getAI()->getSupplyTruckAIInterface();
	return supply == nullptr || !supply->isCurrentlyFerryingSupplies();
}

bool hasActiveBuilderForFoundation(Player* player, ObjectID foundationId)
{
	if (player == nullptr || TheGameLogic == nullptr || static_cast<Int>(foundationId) <= 0)
	{
		return false;
	}
	Object* foundation = TheGameLogic->findObjectByID(foundationId);
	if (foundation == nullptr || foundation->isEffectivelyDead() ||
		foundation->getControllingPlayer() != player ||
		!foundation->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION)) return false;
	Object* recordedBuilder = TheGameLogic->findObjectByID(foundation->getBuilderID());
	if (recordedBuilder != nullptr && recordedBuilder->getControllingPlayer() == player)
	{
		if (recordedBuilder->getAI() != nullptr && recordedBuilder->getAI()->getDozerAIInterface() == nullptr)
		{
			return !recordedBuilder->getAI()->isIdle();
		}
		if (isWorkerBuildingFoundation(recordedBuilder, foundationId))
		{
			return true;
		}
	}
	// The recorded builder only changes once a replacement starts constructing, so a
	// recovery worker still walking to the site must be found by its own build order.
	for (Object* worker = TheGameLogic->getFirstObject(); worker != nullptr; worker = worker->getNextObject())
	{
		if (worker != recordedBuilder && worker->getControllingPlayer() == player
			&& isWorkerBuildingFoundation(worker, foundationId))
		{
			return true;
		}
	}
	return false;
}

void updateStrategicFoundationProgress(Object* foundation, AutonomyStrategicFoundationState& state, DWORD now)
{
	if (foundation == nullptr || (state.firstSeenTick != 0u && state.lastSeenTick == now)) return;
	if (state.firstSeenTick == 0u)
	{
		appendRecordingEvent("construction", "foundation_seen", state.templateName.c_str(), 0, static_cast<unsigned int>(foundation->getID()));
		state.firstSeenTick = now;
		state.lastProgressTick = now;
	}
	state.lastSeenTick = now;
	state.lastHealth = getObjectHealthForStrategicFoundation(foundation);
	state.maxHealth = getObjectMaxHealthForStrategicFoundation(foundation);
	const Coord3D* pos = foundation->getPosition();
	if (pos != nullptr) { state.x = pos->x; state.y = pos->y; state.z = pos->z; }
	if (state.lastHealth < 0.0f) return;
	// Accumulate small frame gains; damage establishes a new baseline, not progress.
	if (state.progressHealth < 0.0f || state.lastHealth > state.progressHealth + 1.0f)
	{
		state.progressHealth = state.lastHealth;
		state.lastProgressTick = now;
		state.reason = "healthy_in_progress";
		if (state.stopIssued)
		{
			appendRecordingEvent("construction", "foundation_progress_resumed", state.templateName.c_str(), 0, static_cast<unsigned int>(foundation->getID()));
			state.stopIssued = false;
			state.recoveryAttempts = 0;
			m_autonomy.taskReservationManager.clearFoundationTombstone(static_cast<unsigned int>(foundation->getID()));
		}
	}
	else if (state.lastHealth < state.progressHealth) state.progressHealth = state.lastHealth;
}

void adoptOrphanFoundations(Player* player)
{
	if (player == nullptr || TheGameLogic == nullptr)
	{
		return;
	}

	const DWORD now = adapterGameplayTick();

	// Scan all owned under-construction structures
	for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead())
		{
			continue;
		}
		if (obj->getControllingPlayer() != player)
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

		const ThingTemplate* objTemplate = obj->getTemplate();
		if (objTemplate == nullptr)
		{
			continue;
		}

		const std::string templateName = objTemplate->getName().str();
		if (!isStrategicStructureTemplate(templateName))
		{
			continue; // Not a strategic structure, skip
		}

		const ObjectID foundationId = obj->getID();

		// Check if this foundation already has an active build task
		std::vector<SpecialTaskReservation*> buildTasks = m_autonomy.taskReservationManager.findBuildTasks();
		bool hasActiveTask = false;
		for (SpecialTaskReservation* task : buildTasks)
		{
			if (task == nullptr)
			{
				continue;
			}
			// Skip terminal states
			if (task->state == SpecialTaskState::Complete ||
				task->state == SpecialTaskState::Failed ||
				task->state == SpecialTaskState::Expired)
			{
				continue;
			}

			// Check if this task owns this foundation
			if (task->targetObjectId == foundationId)
			{
				hasActiveTask = true;
				break;
			}

			// Also check by template and position (for tasks that haven't found foundation yet)
			if (task->expectedTemplate == templateName)
			{
				const Coord3D* objPos = obj->getPosition();
				if (objPos != nullptr)
				{
					const Real dx = objPos->x - task->targetPosition.x;
					const Real dy = objPos->y - task->targetPosition.y;
					const Real distSq = dx * dx + dy * dy;
					if (distSq < 150.0f * 150.0f)
					{
						hasActiveTask = true;
						break;
					}
				}
			}
		}

		if (!hasActiveTask)
		{
			// Orphan foundation detected - adopt it
			const Coord3D* foundationPos = obj->getPosition();
			if (foundationPos == nullptr)
			{
				continue;
			}

			DWORD tombstoneAgeMs = 0u;
			std::string tombstoneReason;
			if (m_autonomy.taskReservationManager.isFoundationTombstoned(
				static_cast<unsigned int>(foundationId),
				now,
				&tombstoneAgeMs,
				&tombstoneReason))
			{
				const Real currentHealth = getObjectHealthForStrategicFoundation(obj);
				const Real maxHealth = getObjectMaxHealthForStrategicFoundation(obj);
				const bool activeWmdThreat = m_autonomy.wmdTargetTracker.hasActiveWMDThreat();
				const Money* wallet = player->getMoney();
				const UnsignedInt currentMoney = wallet != nullptr ? wallet->countMoney() : 0u;
				const auto profilePolicyConfig = resolveAutonomyProfilePolicyConfig();
				const bool reserveProtected = currentMoney >= profilePolicyConfig.reserveCash + 5000u;
				AutonomyStrategicFoundationState& strategicState =
					m_autonomy.state.strategicFoundationHealth[static_cast<UnsignedInt>(foundationId)];
				strategicState.templateName = templateName;
				updateStrategicFoundationProgress(obj, strategicState, now);
				AIControlAdapterStrategicFoundationFact fact = observeStrategicFoundation(
					player, static_cast<UnsignedInt>(foundationId), strategicState, now, activeWmdThreat);
				fact.reason = strategicState.reason.empty() ? tombstoneReason : strategicState.reason;
				const AIControlAdapterStrategicFoundationClassification classification =
					AIControlAdapterStrategicFoundationSurvivalManager().Classify(fact);
				if (classification.recoverable && reserveProtected)
				{
					m_autonomy.taskReservationManager.clearFoundationTombstone(static_cast<unsigned int>(foundationId));
					strategicState.stopIssued = false;
					strategicState.reason = "scud_wmd_recovery_override";
					strategicState.recoveryAttempts = 0;
					strategicState.lastProgressTick = now;
					strategicState.lastRecoveryTick = 0u;
					adapterLog(
						"scud_foundation_tombstone_override foundation=%u issued=1 reason=%s age_ms=%u",
						static_cast<unsigned int>(foundationId),
						classification.reason,
						tombstoneAgeMs);
					adapterLog(
						"scud_foundation_recovery foundation=%u action=resume reason=%s health=%.1f progress=%.2f worker=0 tombstoned=1",
						static_cast<unsigned int>(foundationId),
						"tombstone_override_recovery",
						currentHealth,
						maxHealth > 0.0f ? currentHealth / maxHealth : -1.0f);
				}
				else
				{
					const char* skipReason = !classification.recoverable ? classification.reason : "reserve_protected";
					adapterLog(
						"scud_foundation_tombstone_override foundation=%u issued=0 reason=%s age_ms=%u",
						static_cast<unsigned int>(foundationId),
						skipReason,
						tombstoneAgeMs);
					adapterLog(
						"scud_foundation_recovery foundation=%u action=skip reason=%s health=%.1f progress=%.2f worker=0 tombstoned=1",
						static_cast<unsigned int>(foundationId),
						skipReason,
						currentHealth,
						maxHealth > 0.0f ? currentHealth / maxHealth : -1.0f);
					if (m_autonomy.taskReservationManager.shouldLogFoundationTombstoneSkip(static_cast<unsigned int>(foundationId), now))
					{
						adapterLog(
							"orphan_foundation_skip foundation=%u template=%s reason=tombstoned_%s age_ms=%u",
							static_cast<unsigned int>(foundationId),
							templateName.c_str(),
							tombstoneReason.c_str(),
							tombstoneAgeMs);
					}
					continue;
				}
			}

			const bool hasActiveBuilder = hasActiveBuilderForFoundation(player, foundationId);
			const unsigned int activeBuilderWorkerId = hasActiveBuilder ? static_cast<unsigned int>(obj->getBuilderID()) : 0u;

			// Create adoption task
			const unsigned int taskId = m_autonomy.taskReservationManager.createReservation(
				SpecialTaskType::BuildStructure,
				activeBuilderWorkerId, // 0 if no active builder
				templateName,
				*foundationPos,
				"smart_build_adopted",
				90000u); // 90 second timeout

			// Set foundation as target immediately
			m_autonomy.taskReservationManager.setTaskTargetObject(taskId, foundationId);

			// Update state based on whether builder exists
			if (hasActiveBuilder)
			{
				m_autonomy.taskReservationManager.updateTaskState(taskId, SpecialTaskState::Executing, "orphan_foundation_adopted");
			}
			else
			{
				m_autonomy.taskReservationManager.updateTaskState(taskId, SpecialTaskState::Executing, "orphan_foundation_adopted");
				// Mark as stalled immediately so recovery will be attempted
				SpecialTaskReservation* adoptedTask = m_autonomy.taskReservationManager.findReservation(taskId);
				if (adoptedTask != nullptr)
				{
					adoptedTask->reason = "stalled_no_builder";
					adoptedTask->lastUpdateTick = now - 16000u; // Trigger immediate recovery attempt
				}
			}

			adapterLog(
				"orphan_foundation_adopted task=%u foundation=%u template=%s has_builder=%d builder=%u x=%.1f y=%.1f",
				taskId,
				static_cast<unsigned int>(foundationId),
				templateName.c_str(),
				hasActiveBuilder ? 1 : 0,
				activeBuilderWorkerId,
				foundationPos->x,
				foundationPos->y);

			// Telemetry event
			recordAutonomyTelemetryEvent(
				"construction_task",
				templateName,
				"orphan_foundation_adopted",
				foundationPos);
		}
	}
}

void updateConstructionTaskLifecycle(Player* player)
{
	if (player == nullptr || TheGameLogic == nullptr)
	{
		return;
	}

	const DWORD now = adapterGameplayTick();

	// Prune expired tasks
	m_autonomy.taskReservationManager.pruneExpiredTasks(now);

	// Get all active build tasks
	std::vector<SpecialTaskReservation*> buildTasks = m_autonomy.taskReservationManager.findBuildTasks();

	for (SpecialTaskReservation* task : buildTasks)
	{
		if (task == nullptr)
		{
			continue;
		}

		// Skip terminal states
		if (task->state == SpecialTaskState::Complete ||
			task->state == SpecialTaskState::Failed ||
			task->state == SpecialTaskState::Expired)
		{
			continue;
		}

		// Check if source worker still exists
		Object* worker = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->sourceObjectId));
		const bool workerExists = (worker != nullptr && !worker->isEffectivelyDead());

		// Look for matching under-construction foundation
		Object* foundFoundation = nullptr;

		// CRITICAL FIX: If we already have a targetObjectId (foundation was found previously),
		// use it for direct lookup instead of scanning by template/position
		if (task->targetObjectId != 0)
		{
			Object* targetObj = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->targetObjectId));
			if (targetObj != nullptr && !targetObj->isEffectivelyDead())
			{
				// Verify it's still the right kind of object
				if (targetObj->getControllingPlayer() == player &&
					targetObj->isKindOf(KINDOF_STRUCTURE))
				{
					const ThingTemplate* objTemplate = targetObj->getTemplate();
					if (objTemplate != nullptr && objTemplate->getName().str() == task->expectedTemplate)
					{
						foundFoundation = targetObj;
					}
				}
			}
		}

		// If we don't have a targetObjectId yet (task still in Assigned/Moving),
		// scan for matching foundation by template and position
		if (foundFoundation == nullptr && task->targetObjectId == 0)
		{
			// Never bind a foundation another active build task already owns (two tasks
			// sharing one foundation left the second build untracked); take the nearest.
			std::set<unsigned int> claimedFoundations;
			for (const SpecialTaskReservation* otherTask : m_autonomy.taskReservationManager.findBuildTasks())
			{
				if (otherTask == nullptr || otherTask == task || otherTask->targetObjectId == 0u) continue;
				if (otherTask->state == SpecialTaskState::Complete || otherTask->state == SpecialTaskState::Failed
					|| otherTask->state == SpecialTaskState::Expired) continue;
				claimedFoundations.insert(otherTask->targetObjectId);
			}
			Real bestFoundationDistSq = 150.0f * 150.0f; // 150 units radius tolerance
			for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
			{
				if (obj->isEffectivelyDead())
				{
					continue;
				}
				if (obj->getControllingPlayer() != player)
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

				// Check if template matches
				const ThingTemplate* objTemplate = obj->getTemplate();
				if (objTemplate == nullptr)
				{
					continue;
				}
				const std::string objTemplateName = objTemplate->getName().str();
				if (objTemplateName != task->expectedTemplate)
				{
					continue;
				}

				// Check if position is close to task target
				const Coord3D* objPos = obj->getPosition();
				if (objPos == nullptr)
				{
					continue;
				}
				const Real dx = objPos->x - task->targetPosition.x;
				const Real dy = objPos->y - task->targetPosition.y;
				const Real distSq = dx * dx + dy * dy;
				if (distSq > bestFoundationDistSq)
				{
					continue;
				}
				if (claimedFoundations.count(static_cast<unsigned int>(obj->getID())) != 0u)
				{
					continue;
				}

				// Nearest unclaimed matching foundation so far
				foundFoundation = obj;
				bestFoundationDistSq = distSq;
			}
		}

		// Update task state based on foundation status
		if (foundFoundation != nullptr)
		{
			// Foundation exists
			if (task->state == SpecialTaskState::Assigned || task->state == SpecialTaskState::Moving)
			{
				// Foundation just appeared, move to Executing
				m_autonomy.taskReservationManager.updateTaskState(
					task->taskId,
					SpecialTaskState::Executing,
					"foundation_found");
				m_autonomy.taskReservationManager.setTaskTargetObject(task->taskId, foundFoundation->getID());
				adapterLog(
					"special_task_state task=%u state=Executing reason=foundation_found foundation=%u template=%s",
					task->taskId,
					static_cast<unsigned int>(foundFoundation->getID()),
					task->expectedTemplate.c_str());

				// Telemetry event
				const Coord3D* foundationPos = foundFoundation->getPosition();
				recordAutonomyTelemetryEvent(
					"construction_task",
					task->expectedTemplate,
					"foundation_found",
					foundationPos);
			}
			else if (task->state == SpecialTaskState::Executing)
			{
				// Check if foundation is still under construction
				if (!foundFoundation->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				{
					// Construction complete!
					m_autonomy.taskReservationManager.completeTask(task->taskId, "structure_finished");
					if (isPhase79StrategicFoundationTemplate(task->expectedTemplate))
					{
						m_autonomy.taskReservationManager.clearFoundationTombstone(static_cast<unsigned int>(foundFoundation->getID()));
						m_autonomy.state.strategicFoundationHealth.erase(static_cast<UnsignedInt>(foundFoundation->getID()));
						adapterLog(
							"strategic_foundation_released template=%s foundation=%u reason=completed",
							task->expectedTemplate.c_str(),
							static_cast<unsigned int>(foundFoundation->getID()));
					}
					adapterLog(
						"special_task_state task=%u state=Complete reason=structure_finished foundation=%u template=%s",
						task->taskId,
						static_cast<unsigned int>(foundFoundation->getID()),
						task->expectedTemplate.c_str());

					// Telemetry event
					const Coord3D* finishedPos = foundFoundation->getPosition();
					recordAutonomyTelemetryEvent(
						"construction_task",
						task->expectedTemplate,
						"structure_finished",
						finishedPos);
				}
				else
				{
					// Foundation is still under construction
					// Check if anyone is actively building it
					const ObjectID foundationId = foundFoundation->getID();
					bool hasActiveBuilder = false;
					const bool isPhase79Strategic = isPhase79StrategicFoundationTemplate(task->expectedTemplate);
					if (isPhase79Strategic)
					{
						AutonomyStrategicFoundationState& foundationState =
							m_autonomy.state.strategicFoundationHealth[static_cast<UnsignedInt>(foundationId)];
						foundationState.templateName = task->expectedTemplate;
						updateStrategicFoundationProgress(foundFoundation, foundationState, now);
					}

					hasActiveBuilder = hasActiveBuilderForFoundation(player, foundationId);

					// If no active builder, foundation is stalled - needs recovery
					if (!hasActiveBuilder)
					{
						const DWORD stalledDuration = now - task->lastUpdateTick;
						if (isPhase79Strategic)
						{
							AutonomyStrategicFoundationState& foundationState =
								m_autonomy.state.strategicFoundationHealth[static_cast<UnsignedInt>(foundationId)];
							foundationState.reason = workerExists ? "no_active_builder" : "worker_dead";
						}
						if (stalledDuration > 15000u) // 15 seconds without progress
						{
							adapterLog(
								"construction_task_stalled task=%u template=%s foundation=%u stalled_duration=%u reason=no_active_builder",
								task->taskId,
								task->expectedTemplate.c_str(),
								static_cast<unsigned int>(foundationId),
								stalledDuration);

							// Telemetry event
							const Coord3D* stalledPos = foundFoundation->getPosition();
							recordAutonomyTelemetryEvent(
								"construction_task",
								task->expectedTemplate,
								"foundation_stalled",
								stalledPos);

							// Mark for recovery
							task->reason = "stalled_no_builder";
							task->lastUpdateTick = now;
						}
					}
					else
					{
						// Active builder found, update last seen
						task->reason = "active_builder";
						task->lastSeenTick = now;
						task->lastUpdateTick = now;
					}
				}
			}
		}
		else
		{
			// Foundation not found
			// CRITICAL FIX: Only fail if we had a targetObjectId set (meaning we found it before and now it's gone)
			// Don't fail tasks in Assigned/Moving state just because the foundation hasn't appeared yet
			if (task->state == SpecialTaskState::Executing && task->targetObjectId != 0)
			{
				// We had a foundation tracked by ID, and now it's truly gone
				m_autonomy.taskReservationManager.failTask(task->taskId, "foundation_disappeared");
				if (isPhase79StrategicFoundationTemplate(task->expectedTemplate))
				{
					m_autonomy.taskReservationManager.clearFoundationTombstone(task->targetObjectId);
					AutonomyStrategicFoundationState& foundationState =
						m_autonomy.state.strategicFoundationHealth[static_cast<UnsignedInt>(task->targetObjectId)];
					foundationState.templateName = task->expectedTemplate;
					foundationState.reason = "destroyed";
					adapterLog(
						"strategic_foundation_released template=%s foundation=%u reason=destroyed",
						task->expectedTemplate.c_str(),
						task->targetObjectId);
				}
				adapterLog(
					"special_task_state task=%u state=Failed reason=foundation_disappeared template=%s foundation_was=%u",
					task->taskId,
					task->expectedTemplate.c_str(),
					task->targetObjectId);

				// Telemetry event
				recordAutonomyTelemetryEvent(
					"construction_task",
					task->expectedTemplate,
					"foundation_disappeared",
					&task->targetPosition);
			}
			else if (!workerExists && (task->state == SpecialTaskState::Assigned || task->state == SpecialTaskState::Moving))
			{
				// Worker died before foundation was created
				m_autonomy.taskReservationManager.failTask(task->taskId, "worker_dead_before_foundation");
				adapterLog(
					"special_task_state task=%u state=Failed reason=worker_dead_before_foundation template=%s",
					task->taskId,
					task->expectedTemplate.c_str());

				// Telemetry event
				recordAutonomyTelemetryEvent(
					"construction_task",
					task->expectedTemplate,
					"worker_dead_before_foundation",
					&task->targetPosition);
			}
		}
	}
}

void attemptAbandonedFoundationRecovery(Player* player)
{
	if (player == nullptr || TheGameLogic == nullptr)
	{
		return;
	}
	const DWORD now = adapterGameplayTick();

	// Get all active build tasks
	std::vector<SpecialTaskReservation*> buildTasks = m_autonomy.taskReservationManager.findBuildTasks();

	for (SpecialTaskReservation* task : buildTasks)
	{
		if (task == nullptr)
		{
			continue;
		}

		// Only attempt recovery for stalled executing tasks
		if (task->state != SpecialTaskState::Executing)
		{
			continue;
		}
		if (task->reason != "stalled_no_builder")
		{
			continue;
		}
		if (task->targetObjectId == 0)
		{
			continue;
		}

		// Find the stalled foundation
		Object* foundation = TheGameLogic->findObjectByID(static_cast<ObjectID>(task->targetObjectId));
		if (foundation == nullptr || foundation->isEffectivelyDead())
		{
			continue;
		}
		if (!foundation->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
		{
			continue;
		}

		if (hasActiveBuilderForFoundation(player, foundation->getID()))
		{
			task->reason = "active_builder";
			task->lastSeenTick = now;
			task->lastUpdateTick = now;
			continue;
		}

		const Coord3D* foundationPos = foundation->getPosition();
		if (foundationPos == nullptr)
		{
			continue;
		}
		const bool isPhase79Strategic = isPhase79StrategicFoundationTemplate(task->expectedTemplate);
		AutonomyStrategicFoundationState* strategicState = nullptr;
		if (isPhase79Strategic)
		{
			strategicState = &m_autonomy.state.strategicFoundationHealth[static_cast<UnsignedInt>(foundation->getID())];
			strategicState->templateName = task->expectedTemplate;
			updateStrategicFoundationProgress(foundation, *strategicState, now);
			const DWORD noProgressMs = now - strategicState->lastProgressTick;
			const bool workerDead = (task->sourceObjectId != 0u &&
				(TheGameLogic->findObjectByID(static_cast<ObjectID>(task->sourceObjectId)) == nullptr ||
				 TheGameLogic->findObjectByID(static_cast<ObjectID>(task->sourceObjectId))->isEffectivelyDead()));
			if (strategicState->recoveryAttempts >= 1 && noProgressMs >= 60000u)
			{
				const char* stopReason = workerDead ? "worker_dead" : "stale_no_progress";
				std::string stopCommandReason;
				const bool stopIssued = executeScopedSelectionCommand(player, std::vector<ObjectID>(1, foundation->getID()), stopCommandReason, [&]() -> bool
				{
					GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_DO_STOP);
					if (msg == nullptr)
					{
						stopCommandReason = "message_stream_not_ready";
						return false;
					}
					return true;
				});
				appendRecordingEvent("construction", stopIssued ? "foundation_stop_issued" : "foundation_stop_rejected", stopIssued ? stopReason : stopCommandReason.c_str(), task->taskId, static_cast<unsigned int>(foundation->getID()));
				strategicState->stopIssued = stopIssued;
				strategicState->reason = workerDead ? "stopped_worker_dead" : "stopped_stale_no_progress";
				adapterLog(
					"strategic_foundation_stop template=%s foundation=%u issued=%d reason=%s",
					task->expectedTemplate.c_str(),
					static_cast<unsigned int>(foundation->getID()),
					stopIssued ? 1 : 0,
					stopIssued ? stopReason : stopCommandReason.c_str());
				if (stopIssued)
				{
					m_autonomy.taskReservationManager.tombstoneStoppedFoundation(
						static_cast<unsigned int>(foundation->getID()),
						task->expectedTemplate,
						*foundationPos,
						strategicState->reason,
						now);
					m_autonomy.taskReservationManager.failTask(task->taskId, strategicState->reason);
					adapterLog(
						"strategic_foundation_released template=%s foundation=%u reason=%s",
						task->expectedTemplate.c_str(),
						static_cast<unsigned int>(foundation->getID()),
						strategicState->reason.c_str());
				}
				continue;
			}
			if (strategicState->recoveryAttempts >= 2)
			{
				adapterLog(
					"strategic_foundation_recovery template=%s foundation=%u worker=0 issued=0 reason=max_recovery_attempts",
					task->expectedTemplate.c_str(),
					static_cast<unsigned int>(foundation->getID()));
				continue;
			}
		}

		// Find nearby idle workers to reassign
		Object* bestWorker = nullptr;
		Real bestScore = 999999999.0f;
		Real bestDistSq = 999999999.0f;
		Int foundationZoneId = -1;
		if (m_autonomy.state.telemetryZones.is_array())
		{
			Real bestZoneDistSq = m_autonomy.state.zoneRadius * m_autonomy.state.zoneRadius;
			for (const auto& zone : m_autonomy.state.telemetryZones)
			{
				if (!zone.is_object())
				{
					continue;
				}
				const Real zx = zone.value("center_x", 0.0f);
				const Real zy = zone.value("center_y", 0.0f);
				const Real dx = zx - foundationPos->x;
				const Real dy = zy - foundationPos->y;
				const Real distSq = dx * dx + dy * dy;
				if (distSq < bestZoneDistSq)
				{
					bestZoneDistSq = distSq;
					foundationZoneId = zone.value("anchor_id", -1);
				}
			}
		}

		// Get all active build tasks to check if workers are already assigned
		std::vector<SpecialTaskReservation*> allBuildTasks = m_autonomy.taskReservationManager.findBuildTasks();

		for (Object* worker = TheGameLogic->getFirstObject(); worker != nullptr; worker = worker->getNextObject())
		{
			if (worker->isEffectivelyDead())
			{
				continue;
			}
			if (worker->getControllingPlayer() != player)
			{
				continue;
			}
			if (!worker->isKindOf(KINDOF_DOZER))
			{
				continue;
			}

			const ObjectID workerId = worker->getID();

			// CRITICAL: Do not steal workers from other active build tasks
			bool workerAssignedToOtherTask = false;
			for (SpecialTaskReservation* otherTask : allBuildTasks)
			{
				if (otherTask == nullptr || otherTask == task)
				{
					continue; // Skip this task (we're trying to recover it)
				}
				// Skip terminal states
				if (otherTask->state == SpecialTaskState::Complete ||
					otherTask->state == SpecialTaskState::Failed ||
					otherTask->state == SpecialTaskState::Expired)
				{
					continue;
				}
				// Check if this worker is assigned to another active task
				if (otherTask->sourceObjectId == workerId)
				{
					workerAssignedToOtherTask = true;
					break;
				}
			}

			if (workerAssignedToOtherTask)
			{
				continue; // Do not steal this worker from their current build
			}

			// Check if worker is reserved for another active task
			if (m_autonomy.taskReservationManager.isObjectReserved(workerId))
			{
				// Skip if reserved by a different task/owner
				if (!m_autonomy.taskReservationManager.canUseObjectForTask(workerId, "smart_build_recovery"))
				{
					continue;
				}
			}

			// Prefer idle workers. GLA workers also harvest and are rarely idle, so fall
			// back to one with no pending dozer task (gathering or moving); never take a
			// worker that is constructing or repairing.
			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr)
			{
				continue;
			}
			const bool workerIdle = ai->isIdle();
			if (!workerIdle)
			{
				DozerAIInterface* dozer = ai->getDozerAIInterface();
				if (dozer == nullptr || dozer->isAnyTaskPending())
				{
					continue;
				}
			}
			// The recorded builder already failed to build this foundation.
			if (workerId == foundation->getBuilderID())
			{
				continue;
			}
			const SupplyTruckAIInterface* workerSupply = ai->getSupplyTruckAIInterface();
			if (workerSupply != nullptr && workerSupply->isForcedIntoWantingState())
			{
				continue;
			}
			const bool workerFerrying = workerSupply != nullptr && workerSupply->isCurrentlyFerryingSupplies();

			// Find closest idle worker
			const Coord3D* workerPos = worker->getPosition();
			if (workerPos == nullptr)
			{
				continue;
			}

			const Real dx = workerPos->x - foundationPos->x;
			const Real dy = workerPos->y - foundationPos->y;
			const Real distSq = dx * dx + dy * dy;
			Real score = distSq;
			Int workerZoneId = -1;
			if (foundationZoneId >= 0 && m_autonomy.state.telemetryZones.is_array())
			{
				Real bestZoneDistSq = m_autonomy.state.zoneRadius * m_autonomy.state.zoneRadius;
				for (const auto& zone : m_autonomy.state.telemetryZones)
				{
					if (!zone.is_object())
					{
						continue;
					}
					const Real zx = zone.value("center_x", 0.0f);
					const Real zy = zone.value("center_y", 0.0f);
					const Real zdx = zx - workerPos->x;
					const Real zdy = zy - workerPos->y;
					const Real zoneDistSq = zdx * zdx + zdy * zdy;
					if (zoneDistSq < bestZoneDistSq)
					{
						bestZoneDistSq = zoneDistSq;
						workerZoneId = zone.value("anchor_id", -1);
					}
				}
				if (workerZoneId == foundationZoneId)
				{
					score -= 1000000.0f;
				}
			}
			if (!workerIdle)
			{
				// Ferrying workers may never leave supply mode; use them last.
				score += workerFerrying ? 200000000.0f : 100000000.0f;
			}

			if (distSq < 1800.0f * 1800.0f && score < bestScore)
			{
				bestWorker = worker;
				bestScore = score;
				bestDistSq = distSq;
			}
		}

		if (bestWorker != nullptr)
		{
			// Attempt to reassign worker to complete the foundation
			adapterLog(
					"construction_task_recovery task=%u template=%s foundation=%u action=reassign worker=%u distance=%.1f reason=no_active_builder",
				task->taskId,
				task->expectedTemplate.c_str(),
				static_cast<unsigned int>(foundation->getID()),
				static_cast<unsigned int>(bestWorker->getID()),
				std::sqrt(bestDistSq));
			if (isPhase79Strategic && strategicState != nullptr)
			{
				++strategicState->recoveryAttempts;
				strategicState->lastRecoveryTick = now;
				const char* source = bestScore < bestDistSq ? "same_zone" : (bestDistSq < 1000.0f * 1000.0f ? "nearby_zone" : "global_fallback");
				adapterLog(
					"construction_worker_selection task=%u template=%s zone=%d worker=%u distance=%.1f source=%s reason=strategic_foundation_recovery",
					task->taskId,
					task->expectedTemplate.c_str(),
					foundationZoneId,
					static_cast<unsigned int>(bestWorker->getID()),
					std::sqrt(bestDistSq),
					source);
			}

			// Telemetry event
			recordAutonomyTelemetryEvent(
				"construction_task",
				task->expectedTemplate,
				"worker_reassigned",
				foundationPos);

			// Command worker to BUILD the foundation (not just move near it)
			// Re-issuing MSG_DOZER_CONSTRUCT for the same template/location makes the worker continue building the existing foundation
			const ThingTemplate* foundationTemplate = foundation->getTemplate();
			Player* player = foundation->getControllingPlayer();
			bool buildCommandIssued = false;

			if (foundationTemplate == nullptr)
			{
				adapterLog(
					"construction_task_recovery_no_template task=%u foundation=%u worker=%u fallback=move",
					task->taskId,
					static_cast<unsigned int>(foundation->getID()),
					static_cast<unsigned int>(bestWorker->getID()));
			}
			else if (player == nullptr)
			{
				adapterLog(
					"construction_task_recovery_no_player task=%u foundation=%u worker=%u fallback=move",
					task->taskId,
					static_cast<unsigned int>(foundation->getID()),
					static_cast<unsigned int>(bestWorker->getID()));
			}
			else
			{
				const ObjectID workerId = bestWorker->getID();
				const ObjectID foundationId = foundation->getID();
				std::string buildReason;

				adapterLog(
					"construction_task_recovery_resume_attempt task=%u foundation=%u worker=%u template=%s",
					task->taskId,
					static_cast<unsigned int>(foundationId),
					static_cast<unsigned int>(bestWorker->getID()),
					foundationTemplate->getName().str());

				// The engine refuses a resume while the recorded builder still holds BUILD as its
				// current task on this foundation (a GLA worker can keep it while harvesting), so
				// release that builder with a player Stop first; it is processed before the resume.
				Object* staleBuilder = TheGameLogic->findObjectByID(foundation->getBuilderID());
				if (staleBuilder != nullptr && staleBuilder != bestWorker && !staleBuilder->isEffectivelyDead()
					&& staleBuilder->getControllingPlayer() == player)
				{
					std::string releaseReason;
					const bool released = executeScopedSelectionCommand(player, std::vector<ObjectID>(1, staleBuilder->getID()), releaseReason, [&]() -> bool
					{
						return appendPlayerMessage(player, GameMessage::MSG_DO_STOP) != nullptr;
					});
					adapterLog(
						"construction_task_release_builder task=%u foundation=%u builder=%u replacement=%u issued=%d reason=%s",
						task->taskId,
						static_cast<unsigned int>(foundationId),
						static_cast<unsigned int>(staleBuilder->getID()),
						static_cast<unsigned int>(workerId),
						released ? 1 : 0,
						released ? "stuck_builder" : releaseReason.c_str());
				}

				// Use MSG_RESUME_CONSTRUCTION to continue building an existing foundation
				// (MSG_DO_REPAIR is for damaged buildings, MSG_RESUME_CONSTRUCTION is for unfinished ones)
				if (executeScopedSelectionCommand(player, std::vector<ObjectID>(1, workerId), buildReason, [&]() -> bool
				{
					GameMessage* msg = appendPlayerMessage(player, GameMessage::MSG_RESUME_CONSTRUCTION);
					if (msg == nullptr)
					{
						buildReason = "message_stream_not_ready";
						return false;
					}
					msg->appendObjectIDArgument(foundationId);
					return true;
				}))
				{
					buildCommandIssued = true;
					adapterLog(
						"construction_task_recovery_resume_success task=%u foundation=%u worker=%u template=%s",
						task->taskId,
						static_cast<unsigned int>(foundationId),
						static_cast<unsigned int>(bestWorker->getID()),
						foundationTemplate->getName().str());
				}
				else
				{
					adapterLog(
						"construction_task_recovery_resume_failed task=%u foundation=%u worker=%u reason=%s",
						task->taskId,
						static_cast<unsigned int>(foundationId),
						static_cast<unsigned int>(bestWorker->getID()),
						buildReason.c_str());
				}
			}

			if (!buildCommandIssued)
			{
				// Build command failed, just move worker there as fallback
				std::string moveReason;
				moveWorkerToPosition(bestWorker, foundationPos, moveReason);
			}
			if (isPhase79Strategic)
			{
				adapterLog(
					"strategic_foundation_recovery template=%s foundation=%u worker=%u issued=%d reason=%s",
					task->expectedTemplate.c_str(),
					static_cast<unsigned int>(foundation->getID()),
					static_cast<unsigned int>(bestWorker->getID()),
					buildCommandIssued ? 1 : 0,
					buildCommandIssued ? "replacement_worker" : "resume_failed_move_fallback");
				if (containsIgnoreCase(task->expectedTemplate, "scudstorm"))
				{
					const Real currentHealth = getObjectHealthForStrategicFoundation(foundation);
					const Real maxHealth = getObjectMaxHealthForStrategicFoundation(foundation);
					DWORD tombstoneAgeMs = 0u;
					const bool tombstoned = m_autonomy.taskReservationManager.isFoundationTombstoned(
						static_cast<unsigned int>(foundation->getID()),
						now,
						&tombstoneAgeMs,
						nullptr);
					adapterLog(
						"scud_foundation_recovery foundation=%u action=reassign reason=%s health=%.1f progress=%.2f worker=%u tombstoned=%d",
						static_cast<unsigned int>(foundation->getID()),
						buildCommandIssued ? "resume_construction" : "resume_failed_move_fallback",
						currentHealth,
						maxHealth > 0.0f ? currentHealth / maxHealth : -1.0f,
						static_cast<unsigned int>(bestWorker->getID()),
						tombstoned ? 1 : 0);
				}
			}

			// Update task: reset stalled state and update source worker
			task->sourceObjectId = bestWorker->getID();
			task->reason = "worker_reassigned";
			task->lastUpdateTick = adapterGameplayTick();

			// Reserve the reassigned worker
			reserveWorkerForBuild(bestWorker, 30000u);
		}
		else
		{
			// No suitable worker found for recovery
			const DWORD now = adapterGameplayTick();
			const DWORD stalledDuration = now - task->createdTick;

			if (stalledDuration > 60000u) // 60 seconds total stall - give up
			{
				if (isPhase79Strategic && strategicState != nullptr)
				{
					++strategicState->recoveryAttempts;
					adapterLog(
						"strategic_foundation_recovery template=%s foundation=%u worker=0 issued=0 reason=no_local_worker",
						task->expectedTemplate.c_str(),
						static_cast<unsigned int>(foundation->getID()));
					if (containsIgnoreCase(task->expectedTemplate, "scudstorm"))
					{
						const Real currentHealth = getObjectHealthForStrategicFoundation(foundation);
						const Real maxHealth = getObjectMaxHealthForStrategicFoundation(foundation);
						DWORD tombstoneAgeMs = 0u;
						const bool tombstoned = m_autonomy.taskReservationManager.isFoundationTombstoned(
							static_cast<unsigned int>(foundation->getID()),
							now,
							&tombstoneAgeMs,
							nullptr);
						adapterLog(
							"scud_foundation_recovery foundation=%u action=skip reason=no_local_worker health=%.1f progress=%.2f worker=0 tombstoned=%d",
							static_cast<unsigned int>(foundation->getID()),
							currentHealth,
							maxHealth > 0.0f ? currentHealth / maxHealth : -1.0f,
							tombstoned ? 1 : 0);
					}
					continue;
				}
				m_autonomy.taskReservationManager.failTask(task->taskId, "recovery_timeout_no_worker");
				adapterLog(
					"special_task_state task=%u state=Failed reason=recovery_timeout_no_worker template=%s foundation=%u stalled_duration=%u",
					task->taskId,
					task->expectedTemplate.c_str(),
					static_cast<unsigned int>(foundation->getID()),
					stalledDuration);

				// Telemetry event
				recordAutonomyTelemetryEvent(
					"construction_task",
					task->expectedTemplate,
					"recovery_timeout",
					foundationPos);
			}
		}
	}
}

void logStrategicFoundationHealth(Player* player)
{
	if (player == nullptr || TheGameLogic == nullptr)
	{
		return;
	}

	struct Counts
	{
		int live = 0;
		int inProgress = 0;
		int healthy = 0;
		int stale = 0;
		int noBuilder = 0;
		int damaged = 0;
	};
	std::map<std::string, Counts> countsByTemplate;
	const DWORD now = adapterGameplayTick();

	for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
	{
		if (obj == nullptr || obj->isEffectivelyDead())
		{
			continue;
		}
		if (obj->getControllingPlayer() != player || !obj->isKindOf(KINDOF_STRUCTURE))
		{
			continue;
		}
		const ThingTemplate* tt = obj->getTemplate();
		const std::string templateName = tt != nullptr ? tt->getName().str() : "";
		if (!isPhase79StrategicFoundationTemplate(templateName))
		{
			continue;
		}

		Counts& counts = countsByTemplate[templateName];
		++counts.live;
		const bool underConstruction = obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION);
		if (!underConstruction)
		{
			continue;
		}
		++counts.inProgress;
		const UnsignedInt objectId = static_cast<UnsignedInt>(obj->getID());
		const auto stateIt = m_autonomy.state.strategicFoundationHealth.find(objectId);
		const bool stopped = (stateIt != m_autonomy.state.strategicFoundationHealth.end() && stateIt->second.stopIssued);
		const bool stale = (stateIt != m_autonomy.state.strategicFoundationHealth.end() &&
			(now - stateIt->second.lastProgressTick) >= 60000u);
		const bool hasBuilder = hasActiveBuilderForFoundation(player, obj->getID());
		if (!hasBuilder)
		{
			++counts.noBuilder;
		}
		if (stale || stopped)
		{
			++counts.stale;
		}
		else
		{
			++counts.healthy;
		}
		BodyModuleInterface* body = obj->getBodyModule();
		if (body != nullptr && body->getMaxHealth() > 0.0f && body->getHealth() < body->getMaxHealth() * 0.98f)
		{
			++counts.damaged;
		}
	}

	for (const auto& pair : countsByTemplate)
	{
		const Counts& counts = pair.second;
		const char* reason = counts.stale > 0 ? "stale_no_progress" : (counts.noBuilder > 0 ? "no_active_builder" : "healthy_in_progress");
		adapterLogOnChange("strategic_foundation_health:" + pair.first,
			"strategic_foundation_health template=%s live=%d in_progress=%d healthy=%d stale=%d no_builder=%d damaged=%d reason=%s",
			pair.first.c_str(),
			counts.live,
			counts.inProgress,
			counts.healthy,
			counts.stale,
			counts.noBuilder,
			counts.damaged,
			reason);
	}
}

void updateSpecialTaskReservations(Player* player)
{
	if (!isAutonomyModeActive())
	{
		return;
	}
	if (player == nullptr)
	{
		return;
	}

	// Adopt orphan foundations first (ensures all strategic structures have tasks)
	adoptOrphanFoundations(player);

	// Update construction task lifecycle
	updateConstructionTaskLifecycle(player);

	// Use protected worker-shuttle Technicals for far remote GLA build tasks.
	updateWorkerShuttleAssignments(player);

	// Attempt recovery for abandoned foundations
	attemptAbandonedFoundationRecovery(player);

	logStrategicFoundationHealth(player);
}
