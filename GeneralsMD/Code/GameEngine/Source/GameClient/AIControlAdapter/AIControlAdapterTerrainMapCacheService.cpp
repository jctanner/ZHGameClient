#include "PreRTS.h"

#include "GameClient/AIControlAdapter/AIControlAdapterTerrainMapCacheService.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <istream>

namespace
{
	void logLine(const AIControlAdapterTerrainMapCacheService::LogFn& log, const std::string& line)
	{
		if (log)
		{
			log(line);
		}
	}

	template <typename... Args>
	void logFormatted(const AIControlAdapterTerrainMapCacheService::LogFn& log, const char* format, Args... args)
	{
		char buffer[1024];
		std::snprintf(buffer, sizeof(buffer), format, args...);
		buffer[sizeof(buffer) - 1] = '\0';
		logLine(log, buffer);
	}
}

std::vector<std::string> AIControlAdapterTerrainMapCacheService::buildCandidatePaths(const std::string& key)
{
	std::vector<std::string> paths;
	if (key.empty())
	{
		return paths;
	}
	const std::string filename = key + ".json";
	paths.push_back("projects/data/map-terrain/" + filename);
	paths.push_back("../projects/data/map-terrain/" + filename);
	paths.push_back("../../projects/data/map-terrain/" + filename);
	paths.push_back("../../../projects/data/map-terrain/" + filename);
	paths.push_back("Z:\\home\\jtanner\\workspace\\github\\jctanner.personal\\zero.hour\\projects\\data\\map-terrain\\" + filename);
	paths.push_back("/home/jtanner/workspace/github/jctanner.personal/zero.hour/projects/data/map-terrain/" + filename);
	return paths;
}

bool AIControlAdapterTerrainMapCacheService::readTextFile(const std::string& path, std::string& outText)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in.good())
	{
		return false;
	}
	outText.assign(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	return true;
}

AIControlAdapterTerrainFacts AIControlAdapterTerrainMapCacheService::loadFactsForMap(
	const std::string& mapName,
	const LogFn& log)
{
	const std::string key = AIControlAdapterNormalizeMapFileCacheKey(mapName);
	const std::unordered_map<std::string, AIControlAdapterTerrainFacts>::const_iterator cached = m_cacheByKey.find(key);
	if (cached != m_cacheByKey.end())
	{
		return cached->second;
	}

	AIControlAdapterTerrainFacts facts;
	facts.mapName = mapName;
	facts.source = "unavailable";
	facts.extraction.mapName = mapName;
	facts.extraction.selectedSource = "unavailable";
	facts.extraction.fallbackSource = "none";
	facts.extraction.reason = "cache_not_found";
	facts.mapFileCacheTelemetry = nlohmann::json::object({
		{"loaded", false},
		{"reason", key.empty() ? "empty_map_name" : "cache_not_found"}
	});

	const std::vector<std::string> paths = buildCandidatePaths(key);
	std::string selectedPath;
	std::string body;
	for (std::size_t i = 0; i < paths.size(); ++i)
	{
		if (readTextFile(paths[i], body))
		{
			selectedPath = paths[i];
			break;
		}
	}

	if (selectedPath.empty())
	{
		if (m_loggedKeys.insert(key + ":missing").second)
		{
			const std::string firstPath = paths.empty() ? "" : paths.front();
			logFormatted(
				log,
				"map_file_cache_probe map=%s normalized=%s path=%s found=0 reason=cache_not_found",
				mapName.empty() ? "unknown" : mapName.c_str(),
				key.empty() ? "empty" : key.c_str(),
				firstPath.empty() ? "none" : firstPath.c_str());
			logFormatted(
				log,
				"map_file_cache_unavailable map=%s reason=cache_not_found",
				mapName.empty() ? "unknown" : mapName.c_str());
		}
		m_cacheByKey[key] = facts;
		return facts;
	}

	if (m_loggedKeys.insert(key + ":probe").second)
	{
		logFormatted(
			log,
			"map_file_cache_probe map=%s normalized=%s path=%s found=1 reason=matched_normalized_map_name",
			mapName.empty() ? "unknown" : mapName.c_str(),
			key.empty() ? "empty" : key.c_str(),
			selectedPath.c_str());
	}

	try
	{
		const nlohmann::json parsed = nlohmann::json::parse(body);
		std::string reason;
		if (!AIControlAdapterParseMapFileCacheJson(parsed, mapName, selectedPath, facts, reason))
		{
			if (m_loggedKeys.insert(key + ":parse-failed").second)
			{
				logFormatted(
					log,
					"map_file_cache_unavailable map=%s reason=%s",
					mapName.empty() ? "unknown" : mapName.c_str(),
					reason.empty() ? "parse_error" : reason.c_str());
			}
		}
		else if (m_loggedKeys.insert(key + ":loaded").second)
		{
			logFormatted(
				log,
				"map_file_cache_loaded map=%s path=%s waypoints=%d lanes=%d objects=%d hash=%s",
				mapName.empty() ? "unknown" : mapName.c_str(),
				selectedPath.c_str(),
				facts.mapFileCacheTelemetry.value("waypoints", 0),
				facts.mapFileCacheTelemetry.value("lanes", 0),
				facts.mapFileCacheTelemetry.value("objects", 0),
				facts.mapFileCacheTelemetry.value("map_hash", std::string()).c_str());
		}
	}
	catch (const std::exception& ex)
	{
		facts.mapFileCacheTelemetry = nlohmann::json::object({
			{"loaded", false},
			{"source", selectedPath},
			{"reason", "parse_error"},
			{"error", ex.what()}
		});
		if (m_loggedKeys.insert(key + ":exception").second)
		{
			logFormatted(
				log,
				"map_file_cache_unavailable map=%s reason=parse_error",
				mapName.empty() ? "unknown" : mapName.c_str());
		}
	}

	m_cacheByKey[key] = facts;
	return facts;
}

void AIControlAdapterTerrainMapCacheService::clear()
{
	m_cacheByKey.clear();
	m_loggedKeys.clear();
}
