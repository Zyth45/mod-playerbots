/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_COALEAVEINSTANCE_H
#define _PLAYERBOT_COALEAVEINSTANCE_H

#include "Common.h"

class Player;

// A player who leaves a dungeon or a raid alive (Dungeon Finder Leave, teleport, hearthstone, exit portal) takes the
// bots of the group still in that instance along: 2.5 s later they join the player. Random bots of a Dungeon Finder
// group leave the group and the instance instead. Not on a death (corpse run), not in battlegrounds or arenas, and
// not for bots on a CoA mission.
namespace CoaLeaveInstance
{
    // Player hook before a teleport: records the bots to take along when the player leaves an instance.
    void OnBeforeTeleport(Player* player, uint32 mapId);
    // Player update (the player's map thread): once the delay is over, hands the moves to the world thread.
    void OnUpdate(Player* player);
}

#endif
