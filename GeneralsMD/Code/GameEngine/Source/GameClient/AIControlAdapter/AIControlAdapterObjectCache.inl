	/**
	 * AIControlAdapterObjectCache.inl
	 *
	 * Object cache for efficient game state tracking in AI Control Adapter.
	 *
	 * The object cache maintains a snapshot of the player's units and buildings to avoid
	 * expensive full game state traversals every frame. This improves performance when:
	 * - Autonomy mode evaluates macro/production decisions (every 100-500ms)
	 * - Automation rules check unit counts (every frame with cooldowns)
	 * - External queries request game state frequently
	 *
	 * WHY CACHING MATTERS
	 * ===================
	 *
	 * Without caching:
	 * - Every query/decision traverses all game objects (expensive)
	 * - player->countObjects() walks entire object list
	 * - player->iterateObjects() processes every unit/building
	 * - Multiple systems doing this independently = wasted CPU
	 *
	 * With caching:
	 * - Snapshot built once per frame (or on-demand)
	 * - All queries/decisions read cached data (fast lookup)
	 * - Incremental updates possible (only refresh when objects change)
	 * - Reduced redundant work when multiple systems need same data
	 *
	 * CACHE ARCHITECTURE
	 * ==================
	 *
	 * Cache structure:
	 * - units: JSON array of all non-building objects (units)
	 * - buildings: JSON array of all buildings
	 * - idleWorkers: JSON array of workers not currently busy
	 * - Counts cached separately for fast access (unitsTotal, buildingsTotal, idleWorkersTotal)
	 *
	 * Cache lifecycle:
	 * 1. Initial state: valid=false (empty cache)
	 * 2. First query: Cache built by traversing game objects
	 * 3. Subsequent queries: Read from cache (fast)
	 * 4. Invalidation: valid=false when objects change
	 * 5. Next query: Cache rebuilt
	 *
	 * Invalidation triggers:
	 * - Object created/destroyed
	 * - Building completed
	 * - Unit production finished
	 * - Worker assignment changed
	 * - Game reset (return to menu, new match)
	 *
	 * CACHE REFRESH STRATEGY
	 * ======================
	 *
	 * Two refresh approaches:
	 *
	 * 1. Lazy refresh (current implementation):
	 *    - Cache invalidated when objects change
	 *    - Rebuilt on next query/decision that needs data
	 *    - Pros: Only refresh when needed
	 *    - Cons: First query after invalidation pays rebuild cost
	 *
	 * 2. Periodic refresh (alternative):
	 *    - Cache refreshed every N frames or milliseconds
	 *    - Pros: Predictable refresh cost, spread across frames
	 *    - Cons: May refresh unnecessarily if no queries between refreshes
	 *
	 * Current implementation uses lazy refresh because:
	 * - Queries are frequent (autonomy/automation systems run often)
	 * - Rebuild cost is low (100-200 objects typical)
	 * - Avoids refresh work when adapter idle (manual mode, no queries)
	 *
	 * VERSIONING
	 * ==========
	 *
	 * The cache version increments each rebuild:
	 * - Allows external systems to detect cache updates
	 * - Can be used for change detection (version != lastSeenVersion)
	 * - Future use: Event-driven updates when cache refreshes
	 *
	 * PERFORMANCE
	 * ===========
	 *
	 * Cache rebuild cost:
	 * - Typical game: 50-150 objects = ~0.2-0.5ms
	 * - Large game: 300+ objects = ~1-2ms
	 * - Cache read: <0.01ms (JSON array lookup)
	 *
	 * Impact on query performance:
	 * - Without cache: Every Game.Query traverses all objects (~0.5-2ms)
	 * - With cache: First query pays rebuild cost, subsequent reads ~0.01ms
	 * - Net win: If 10+ queries between rebuilds (common in autonomy mode)
	 *
	 * CACHE CONTENTS
	 * ==============
	 *
	 * Each cached object includes:
	 * - id: Unique object ID (for tracking specific units)
	 * - name: Template name (e.g., "GLAVehicleScorpionTank")
	 * - position: {x, y, z} map coordinates
	 * - health: Current health value
	 * - health_max: Maximum health value
	 * - is_damaged: True if health < max health
	 * - is_under_construction: True if building/being built
	 * - is_selling: True if building being sold
	 * - production_queue: (buildings only) Units queued for production
	 *
	 * Idle worker criteria:
	 * - Is worker/dozer unit
	 * - Not dead or under construction
	 * - Not currently performing an action (building, repairing, moving)
	 * - Available for new work assignments
	 *
	 * See also:
	 * - AIControlAdapterGameQuery.inl for query system that uses this cache
	 * - AIControlAdapterWorkers.inl for worker management that reads idle worker cache
	 * - AIControlAdapter.cpp update() for cache invalidation logic
	 */

	/**
	 * Object cache for player's units and buildings.
	 *
	 * Maintains a snapshot of game objects to avoid expensive traversals.
	 * Cached data is used by queries, autonomy system, and automation rules.
	 */
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

		/**
		 * Reset cache to empty state.
		 *
		 * Called when:
		 * - Adapter initializes
		 * - Game resets (new match, return to menu)
		 * - Player changes (switching controlled player)
		 */
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

		/**
		 * Invalidate cache, forcing rebuild on next access.
		 *
		 * Called when:
		 * - Object created or destroyed
		 * - Building construction completed
		 * - Unit production finished
		 * - Worker assignment changed
		 *
		 * Does NOT immediately rebuild - rebuild happens lazily on next query/decision
		 * that needs cached data. This defers the rebuild cost until actually needed.
		 */
		void invalidate()
		{
			valid = false;
		}

		bool valid;                  ///< True if cache is current, false if needs rebuild
		Int playerIndex;             ///< Player index this cache belongs to (-1 if unset)
		Int unitsTotal;              ///< Cached count of all units (excludes buildings)
		Int buildingsTotal;          ///< Cached count of all buildings
		Int idleWorkersTotal;        ///< Cached count of idle workers available for work
		UnsignedInt version;         ///< Cache version, increments on each rebuild
		DWORD lastRefreshTick;       ///< Game tick when cache was last rebuilt
		nlohmann::json units;        ///< JSON array of all units with details
		nlohmann::json buildings;    ///< JSON array of all buildings with production queues
		nlohmann::json idleWorkers;  ///< JSON array of idle workers available for assignment
	};
