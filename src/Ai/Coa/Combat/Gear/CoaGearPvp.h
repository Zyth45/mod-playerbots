/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAGEARPVP_H
#define _PLAYERBOT_COAGEARPVP_H

#include "Define.h"

// AiPlayerbot.CoaCombat.GearPvpSoldOnly: the gear a bot is given never holds a PvP piece a player cannot get. A PvP
// piece (resilience, or an honor rank required) counts only if a vendor, quest, loot table, cache or reward table gives
// it, or gives the base piece it is a variant of (CoaGearPvpRule). The Grand Marshal's
// and High Warlord's sets are none of these on CoA, yet bots wore them.
namespace CoaGearPvp
{
    // Reads the list from the world database, once (at start-up when the key is on, else at the first use).
    void Load();
    // The PvP pieces nobody can get.
    bool IsUnobtainablePvpItem(uint32 itemId);
}

#endif
