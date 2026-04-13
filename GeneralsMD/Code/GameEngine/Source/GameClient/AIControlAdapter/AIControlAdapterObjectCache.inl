	class AdapterObjectCache
	{
	public:
		AdapterObjectCache() :
			valid(false),
			playerIndex(-1),
			unitsTotal(0),
			buildingsTotal(0),
			idleWorkersTotal(0),
			version(0),
			lastRefreshTick(0u)
		{
		}

		void reset()
		{
			valid = false;
			playerIndex = -1;
			unitsTotal = 0;
			buildingsTotal = 0;
			idleWorkersTotal = 0;
			units = nlohmann::json::array();
			buildings = nlohmann::json::array();
			idleWorkers = nlohmann::json::array();
		}

		void invalidate()
		{
			valid = false;
		}

		bool valid;
		Int playerIndex;
		Int unitsTotal;
		Int buildingsTotal;
		Int idleWorkersTotal;
		UnsignedInt version;
		DWORD lastRefreshTick;
		nlohmann::json units;
		nlohmann::json buildings;
		nlohmann::json idleWorkers;
	};
