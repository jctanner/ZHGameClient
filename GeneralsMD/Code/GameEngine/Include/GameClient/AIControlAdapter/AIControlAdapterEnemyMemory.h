#pragma once

#include "GameNetwork/GeneralsOnline/json.hpp"

#include <string>
#include <vector>

enum class EnemyMemoryKind
{
	BaseCommand,
	Production,
	Economy,
	Defense,
	Wmd,
	Army,
	Unknown
};

struct EnemyMemoryPosition
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct EnemyMemoryObservation
{
	unsigned int objectId = 0;
	int playerIndex = -1;
	int team = -1;
	std::string templateName;
	bool isStructure = false;
	bool isUnit = false;
	EnemyMemoryPosition position;
	unsigned int seenTick = 0;
};

struct EnemyMemoryItem
{
	unsigned int objectId = 0;
	int playerIndex = -1;
	int team = -1;
	EnemyMemoryKind kind = EnemyMemoryKind::Unknown;
	std::string templateName;
	bool visible = false;
	bool stale = true;
	bool isStructure = false;
	EnemyMemoryPosition position;
	unsigned int firstSeenTick = 0;
	unsigned int lastSeenTick = 0;
};

struct EnemyMemoryCluster
{
	int playerIndex = -1;
	int team = -1;
	EnemyMemoryKind kind = EnemyMemoryKind::Army;
	int visibleCount = 0;
	EnemyMemoryPosition position;
	unsigned int lastSeenTick = 0;
};

class AIControlAdapterEnemyMemory
{
public:
	void beginUpdate(unsigned int currentTick);
	void observe(const EnemyMemoryObservation& observation);
	void finishUpdate(unsigned int currentTick);
	void clear();

	const std::vector<EnemyMemoryItem>& getItems() const { return m_items; }
	const std::vector<EnemyMemoryCluster>& getClusters() const { return m_clusters; }

	nlohmann::json buildTelemetry(unsigned int currentTick) const;

	static EnemyMemoryKind classifyTemplate(const std::string& templateName, bool isStructure);
	static const char* kindToString(EnemyMemoryKind kind);
	static const char* threatForKind(EnemyMemoryKind kind);
	static bool shouldTrackIndividualItem(EnemyMemoryKind kind, bool isStructure);

private:
	struct UnitObservation
	{
		int playerIndex = -1;
		int team = -1;
		EnemyMemoryPosition position;
		unsigned int seenTick = 0;
	};

	std::vector<EnemyMemoryItem> m_items;
	std::vector<UnitObservation> m_visibleUnits;
	std::vector<EnemyMemoryCluster> m_clusters;
	unsigned int m_updateTick = 0;

	void rebuildClusters(unsigned int currentTick);
};
