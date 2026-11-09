/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: the maps and zones where a real player is, found every 10 s on the world thread and read by the
// bots' AI in the map threads (AiPlayerbot.CoaWorld.ActiveZones.Enable, IdleBotAI.Enable).

#ifndef PLAYERBOTS_COAWORLDACTIVEZONES_H
#define PLAYERBOTS_COAWORLDACTIVEZONES_H

#include "Define.h"

namespace CoaWorldActiveZones
{
// The cache is kept (ActiveZones or IdleBotAI on) and has been filled at least once.
bool Ready();
// A real player (not an invisible GM) is on this map / in this zone of this map.
bool OnMap(uint32 mapId);
bool InZone(uint32 mapId, uint32 zoneId);
}  // namespace CoaWorldActiveZones

void AddSC_coa_world_active_zones();

#endif
