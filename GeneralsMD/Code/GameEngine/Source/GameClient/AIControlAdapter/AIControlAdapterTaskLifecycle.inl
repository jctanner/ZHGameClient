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

bool hasActiveBuilderForFoundation(Player* player, ObjectID foundationId)
{
	if (player == nullptr || TheGameLogic == nullptr || static_cast<Int>(foundationId) <= 0)
	{
		return false;
	}
	for (Object* worker = TheGameLogic->getFirstObject(); worker != nullptr; worker = worker->getNextObject())
	{
		if (worker == nullptr || worker->isEffectivelyDead())
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
		if (worker->getBuilderID() == foundationId)
		{
			return true;
		}
	}
	return false;
}

void adoptOrphanFoundations(Player* player)
{
	if (player == nullptr || TheGameLogic == nullptr)
	{
		return;
	}

	const DWORD now = ::GetTickCount();

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

			// Check if anyone is actively building it
			bool hasActiveBuilder = false;
			unsigned int activeBuilderWorkerId = 0;
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
				if (worker->getBuilderID() == foundationId)
				{
					hasActiveBuilder = true;
					activeBuilderWorkerId = worker->getID();
					break;
				}
			}

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

	const DWORD now = ::GetTickCount();

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
				if (distSq > 150.0f * 150.0f) // 150 units radius tolerance
				{
					continue;
				}

				// Found matching foundation
				foundFoundation = obj;
				break;
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
						if (foundationState.firstSeenTick == 0u)
						{
							foundationState.firstSeenTick = now;
							foundationState.lastProgressTick = now;
						}
						foundationState.templateName = task->expectedTemplate;
						foundationState.lastSeenTick = now;
						const Real currentHealth = getObjectHealthForStrategicFoundation(foundFoundation);
						if (foundationState.lastHealth < 0.0f || currentHealth > foundationState.lastHealth + 1.0f)
						{
							foundationState.lastProgressTick = now;
							foundationState.reason = "healthy_in_progress";
						}
						foundationState.lastHealth = currentHealth;
					}

					// Check if original worker is building it
					if (workerExists)
					{
						AIUpdateInterface* ai = worker->getAI();
						if (ai != nullptr && !ai->isIdle())
						{
							// Check if worker's builder target matches this foundation
							const ObjectID builderTarget = worker->getBuilderID();
							if (builderTarget == foundationId)
							{
								hasActiveBuilder = true;
							}
						}
					}

					// Check if any other worker is building it
					if (!hasActiveBuilder)
					{
						for (Object* otherWorker = TheGameLogic->getFirstObject(); otherWorker != nullptr; otherWorker = otherWorker->getNextObject())
						{
							if (otherWorker->isEffectivelyDead())
							{
								continue;
							}
							if (otherWorker->getControllingPlayer() != player)
							{
								continue;
							}
							if (!otherWorker->isKindOf(KINDOF_DOZER))
							{
								continue;
							}
							if (otherWorker->getBuilderID() == foundationId)
							{
								hasActiveBuilder = true;
								break;
							}
						}
					}

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
	const DWORD now = ::GetTickCount();

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
			if (strategicState->firstSeenTick == 0u)
			{
				strategicState->firstSeenTick = now;
				strategicState->lastProgressTick = now;
			}
			strategicState->templateName = task->expectedTemplate;
			strategicState->lastSeenTick = now;
			const Real currentHealth = getObjectHealthForStrategicFoundation(foundation);
			if (strategicState->lastHealth < 0.0f || currentHealth > strategicState->lastHealth + 1.0f)
			{
				strategicState->lastProgressTick = now;
			}
			strategicState->lastHealth = currentHealth;

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

			// Prefer idle workers
			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr || !ai->isIdle())
			{
				continue;
			}

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
			}

			// Update task: reset stalled state and update source worker
			task->sourceObjectId = bestWorker->getID();
			task->reason = "worker_reassigned";
			task->lastUpdateTick = ::GetTickCount();

			// Reserve the reassigned worker
			reserveWorkerForBuild(bestWorker, 30000u);
		}
		else
		{
			// No suitable worker found for recovery
			const DWORD now = ::GetTickCount();
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
	const DWORD now = ::GetTickCount();

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
		adapterLog(
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

	// Attempt recovery for abandoned foundations
	attemptAbandonedFoundationRecovery(player);

	logStrategicFoundationHealth(player);
}
