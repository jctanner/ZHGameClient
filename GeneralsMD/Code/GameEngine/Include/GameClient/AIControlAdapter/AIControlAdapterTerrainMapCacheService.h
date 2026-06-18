#pragma once

#include "GameClient/AIControlAdapter/AIControlAdapterPolicy.h"

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class AIControlAdapterTerrainMapCacheService
{
public:
	using LogFn = std::function<void(const std::string&)>;

	AIControlAdapterTerrainFacts loadFactsForMap(const std::string& mapName, const LogFn& log);
	void clear();

	static std::vector<std::string> buildCandidatePaths(const std::string& key);
	static bool readTextFile(const std::string& path, std::string& outText);

private:
	std::unordered_map<std::string, AIControlAdapterTerrainFacts> m_cacheByKey;
	std::set<std::string> m_loggedKeys;
};
