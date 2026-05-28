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

		// Find nearby idle workers to reassign
		Object* bestWorker = nullptr;
		Real bestDistSq = 9999999.0f;

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

			if (distSq < bestDistSq && distSq < 1000.0f * 1000.0f) // Within 1000 units
			{
				bestWorker = worker;
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
}
