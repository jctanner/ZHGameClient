#pragma once

#include "Lib/BaseType.h"

#include <string>
#include <vector>

//==============================================================================
// Phase 7.4: Defensive WMD Counterbattery
//
// Detects and tracks enemy superweapon structures (nukes, particle cannons,
// SCUD storms) as critical threats requiring immediate counterbattery response.
//
// The bot must not rely solely on capture for WMD threats. Direct SCUD
// counterbattery and bounded direct strikes are required to prevent mass
// army destruction from enemy superweapons.
//==============================================================================

enum class WMDTargetPriority
{
	Critical,   // Active enemy WMD structure
	High,       // WMD structure under construction
	Medium      // Last known position, not visible
};

struct WMDTarget
{
	unsigned int objectId = 0;
	std::string templateName;
	int ownerIndex = -1;
	Coord3D position;
	WMDTargetPriority priority = WMDTargetPriority::Critical;
	bool visible = false;
	bool alive = true;
	DWORD lastSeenTick = 0;
	DWORD detectedTick = 0;
};

class AIControlAdapterWMDTargetTracker
{
public:
	AIControlAdapterWMDTargetTracker();

	// Detect and track enemy WMD structures
	void updateWMDTargets(class Player* player, DWORD currentTick);

	// Query WMD threats
	bool hasActiveWMDThreat() const;
	const WMDTarget* getHighestPriorityTarget() const;
	const std::vector<WMDTarget>& getAllTargets() const { return m_targets; }
	int getActiveWMDCount() const;

	// Check if a template is a WMD structure
	static bool isWMDTemplate(const std::string& templateName)
	{
		if (templateName.find("NuclearMissile") != std::string::npos)
		{
			return true;
		}
		if (templateName.find("NukeSilo") != std::string::npos)
		{
			return true;
		}
		if (templateName.find("ParticleCannon") != std::string::npos)
		{
			return true;
		}
		if (templateName.find("ScudStorm") != std::string::npos)
		{
			return true;
		}
		return false;
	}

	// Clear all tracked targets
	void clear();

private:
	std::vector<WMDTarget> m_targets;

	// Update existing target or add new one
	void updateTarget(
		unsigned int objectId,
		const std::string& templateName,
		int ownerIndex,
		const Coord3D& position,
		bool visible,
		bool alive,
		DWORD currentTick);

	// Remove targets that are no longer threats
	void pruneStaleTargets(DWORD currentTick);
};
