/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAGEARPVP_H
#define _PLAYERBOT_COAGEARPVP_H

#include "Define.h"

// AiPlayerbot.CoaCombat.GearPvpSoldOnly: the gear a bot is given never holds a PvP piece a player cannot get. A PvP
// piece (resilience, or an honor rank required) counts only if a vendor sells it, a quest rewards it or a loot table
// drops it: the Grand Marshal's and High Warlord's sets are none of these on CoA, yet bots wore them.
namespace CoaGearPvp
{
    // The PvP pieces no vendor, quest or loot gives, read once from the world database.
    bool IsUnobtainablePvpItem(uint32 itemId);
}

#endif
