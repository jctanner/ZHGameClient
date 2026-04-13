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
			const DWORD now = ::GetTickCount();
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
			const DWORD now = ::GetTickCount();
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
			m_workers.reservedWorkersUntilTick[id] = ::GetTickCount() + durationMs;
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
			if (worker->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
			{
				return false;
			}
			AIUpdateInterface* ai = worker->getAI();
			if (ai == nullptr || !ai->isIdle() || ai->isBusy())
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
			return true;
		}

		bool isWorkerFallbackAvailableForNewBuild(Object* worker, bool checkReservation = true)
		{
			if (worker == nullptr || worker->isEffectivelyDead())
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
			if (ctx.firstDozer == nullptr)
			{
				reason = "worker_not_found";
				return nullptr;
			}
			return ctx.firstDozer;
		}
