#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterWMDTarget.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

AIControlAdapterWMDTargetTracker::AIControlAdapterWMDTargetTracker()
{
}

void AIControlAdapterWMDTargetTracker::updateWMDTargets(Player* player, DWORD currentTick)
{
	if (player == nullptr || TheGameLogic == nullptr)
	{
		return;
	}

	// Scan all visible objects for enemy WMD structures.
	for (Object* obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
	{
		if (obj == nullptr || obj->isEffectivelyDead())
		{
			continue;
		}

		const ThingTemplate* objectTemplate = obj->getTemplate();
		const std::string templateName = objectTemplate != nullptr
			? objectTemplate->getName().str()
			: "";

		if (templateName.empty() || !isWMDTemplate(templateName))
		{
			continue;
		}

		// Only enemy-owned WMD structures count as threats
		Player* owner = obj->getControllingPlayer();
		if (owner == nullptr || owner == player || owner->getDefaultTeam() == nullptr ||
			player->getRelationship(owner->getDefaultTeam()) != ENEMIES)
		{
			continue;
		}

		const Coord3D* position = obj->getPosition();
		if (position == nullptr)
		{
			continue;
		}

		// Check if currently visible or partially visible to this player.
		const ObjectShroudStatus shroudStatus = obj->getShroudedStatus(player->getPlayerIndex());
		const bool visible = shroudStatus == OBJECTSHROUD_CLEAR || shroudStatus == OBJECTSHROUD_PARTIAL_CLEAR;
		const bool alive = !obj->isEffectivelyDead();

		updateTarget(
			obj->getID(),
			templateName,
			owner->getPlayerIndex(),
			*position,
			visible,
			alive,
			currentTick);
	}

	// Remove stale targets
	pruneStaleTargets(currentTick);
}

void AIControlAdapterWMDTargetTracker::updateTarget(
	unsigned int objectId,
	const std::string& templateName,
	int ownerIndex,
	const Coord3D& position,
	bool visible,
	bool alive,
	DWORD currentTick)
{
	// Find existing target
	for (std::size_t i = 0; i < m_targets.size(); ++i)
	{
		if (m_targets[i].objectId == objectId)
		{
			// Update existing target
			m_targets[i].position = position;
			m_targets[i].visible = visible;
			m_targets[i].alive = alive;
			m_targets[i].lastSeenTick = currentTick;

			// Update priority based on state
			if (visible && alive)
			{
				m_targets[i].priority = WMDTargetPriority::Critical;
			}
			else if (alive)
			{
				m_targets[i].priority = WMDTargetPriority::Medium;
			}

			return;
		}
	}

	// Add new target
	WMDTarget newTarget;
	newTarget.objectId = objectId;
	newTarget.templateName = templateName;
	newTarget.ownerIndex = ownerIndex;
	newTarget.position = position;
	newTarget.visible = visible;
	newTarget.alive = alive;
	newTarget.lastSeenTick = currentTick;
	newTarget.detectedTick = currentTick;
	newTarget.priority = visible && alive ? WMDTargetPriority::Critical : WMDTargetPriority::Medium;

	m_targets.push_back(newTarget);
}

void AIControlAdapterWMDTargetTracker::pruneStaleTargets(DWORD currentTick)
{
	const DWORD staleThresholdMs = 60000; // 1 minute without update = remove

	for (std::size_t i = 0; i < m_targets.size(); )
	{
		WMDTarget& target = m_targets[i];

		// Check if target is stale
		const DWORD age = currentTick - target.lastSeenTick;
		const bool stale = age > staleThresholdMs;

		// Check if target is no longer alive
		if (!target.alive || stale)
		{
			// Remove target
			m_targets.erase(m_targets.begin() + i);
			continue;
		}

		++i;
	}
}

bool AIControlAdapterWMDTargetTracker::hasActiveWMDThreat() const
{
	for (std::size_t i = 0; i < m_targets.size(); ++i)
	{
		if (m_targets[i].alive &&
			(m_targets[i].priority == WMDTargetPriority::Critical ||
			 m_targets[i].priority == WMDTargetPriority::High))
		{
			return true;
		}
	}
	return false;
}

const WMDTarget* AIControlAdapterWMDTargetTracker::getHighestPriorityTarget() const
{
	const WMDTarget* best = nullptr;

	for (std::size_t i = 0; i < m_targets.size(); ++i)
	{
		const WMDTarget& target = m_targets[i];

		if (!target.alive)
		{
			continue;
		}

		if (best == nullptr || static_cast<int>(target.priority) < static_cast<int>(best->priority))
		{
			best = &target;
		}
	}

	return best;
}

int AIControlAdapterWMDTargetTracker::getActiveWMDCount() const
{
	int count = 0;
	for (std::size_t i = 0; i < m_targets.size(); ++i)
	{
		if (m_targets[i].alive)
		{
			++count;
		}
	}
	return count;
}

void AIControlAdapterWMDTargetTracker::clear()
{
	m_targets.clear();
}
