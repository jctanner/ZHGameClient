		struct WorkerSearchContext
		{
			AIControlAdapterState* self;
			std::vector<Object*> availableIdleDozers;
			Object* firstDozer;
		};

		void pruneExpiredWorkerReservations()
		{
			if (m_reservedWorkersUntilTick.empty())
			{
				return;
			}
			const DWORD now = ::GetTickCount();
			for (auto it = m_reservedWorkersUntilTick.begin(); it != m_reservedWorkersUntilTick.end(); )
			{
				const DWORD untilTick = it->second;
				// Handles tick wrap correctly with signed subtraction.
				if (static_cast<LONG>(untilTick - now) <= 0)
				{
					it = m_reservedWorkersUntilTick.erase(it);
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
			const auto it = m_reservedWorkersUntilTick.find(id);
			if (it == m_reservedWorkersUntilTick.end())
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
			m_reservedWorkersUntilTick[id] = ::GetTickCount() + durationMs;
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
						// Explicit worker selections are allowed to bypass temporary reservation,
						// so internal multi-step smart-build flows can reuse the same worker id.
						if (!isWorkerAvailableForNewBuild(worker, false))
						{
							reason = "worker_not_idle";
							return nullptr;
						}
					}
					return worker;
				}
			}

			WorkerSearchContext ctx = { this, std::vector<Object*>(), nullptr };
			player->iterateObjects(findWorkerCallback, &ctx);
			if (requireIdle)
			{
				if (ctx.availableIdleDozers.empty())
				{
					reason = "idle_worker_not_found";
					return nullptr;
				}

				Object* selected = ctx.availableIdleDozers.front();
				const Int playerIndex = player->getPlayerIndex();
				const auto lastIt = m_lastSelectedWorkerByPlayer.find(playerIndex);
				if (lastIt != m_lastSelectedWorkerByPlayer.end() && ctx.availableIdleDozers.size() > 1u)
				{
					const ObjectID lastId = lastIt->second;
					for (std::size_t i = 0; i < ctx.availableIdleDozers.size(); ++i)
					{
						if (ctx.availableIdleDozers[i] == nullptr || ctx.availableIdleDozers[i]->getID() != lastId)
						{
							continue;
						}
						const std::size_t nextIndex = (i + 1u) % ctx.availableIdleDozers.size();
						selected = ctx.availableIdleDozers[nextIndex];
						break;
					}
				}

				if (selected != nullptr)
				{
					m_lastSelectedWorkerByPlayer[playerIndex] = selected->getID();
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
