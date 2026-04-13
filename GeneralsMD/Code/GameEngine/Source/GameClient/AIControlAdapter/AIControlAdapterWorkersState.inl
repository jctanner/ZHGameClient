	class AdapterWorkersState
	{
	public:
		AdapterWorkersState()
		{
		}

		void reset()
		{
			reservedWorkersUntilTick.clear();
			lastSelectedWorkerByPlayer.clear();
		}

		std::unordered_map<ObjectID, DWORD> reservedWorkersUntilTick;
		std::unordered_map<Int, ObjectID> lastSelectedWorkerByPlayer;
	};
