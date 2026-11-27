/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COASAFEMOVE_H
#define _PLAYERBOT_COASAFEMOVE_H

#include "CoaSafeMoveRules.h"
#include "ObjectGuid.h"

class Player;
class WorldObject;

namespace CoaSafeMove
{
// Moves the bot to the point along the navmesh; false, and no move, when no whole path leads there (the core would
// go in a straight line through walls). allowSwim: a straight swim is allowed when the bot is in water and sees
// the point (out of water toward the shore).
bool MovePoint(Player* bot, float x, float y, float z, bool allowSwim = false);
// True when the core would follow a path to the point, not a straight line through walls (no move made).
// 3.2: straight, when set, tells the move was accepted as a flat line in sight (FlatLineArrives): it is to be made
// without a path (the core's path would stop at the dead end).
bool CanWalkTo(Player* bot, float x, float y, float z, bool* straight = nullptr);
// 3.2: a stop by the NPC (or object) the bot can walk to. When no whole path leads to (x, y, z), the end of the path
// to the NPC is taken if it stands within talking range of it (StandsByNpc) and sees it; false, and x, y, z left
// alone, otherwise.
bool StandByNpc(Player* bot, WorldObject const* npc, float& x, float& y, float& z);
// 3.2 (BuyLog): why StandByNpc found no stop (NoWayReason), and how far the path's end stays from the NPC.
char const* NoWayWhy(Player* bot, WorldObject const* npc, float& endToNpc);
// 3.2 (living bench, Stormwind: Chilton's stop lies off the part of the navmesh the street reaches): another
// auctioneer within AUCTIONEERS_RANGE yards of npc with a walkable stop (StandByNpc), or an empty guid.
ObjectGuid OtherReachableAuctioneer(Player* bot, WorldObject* npc);
// 3.2.2: a bot whose way to npc found no stop: when it stands on a closed island (its path to the stop another bot
// walked to stops short), gets it off (UnstickChoice) and says how; NotStuck when there is no known stop or no island.
Unstick UnstickFromIsland(Player* bot, WorldObject const* npc);
// 3.2.2: (x, y, z) lies in a banker's or an auctioneer's booth, behind its counter (InBooth).
bool InNpcBooth(Player* bot, float x, float y, float z);
// 3.2.3 (NpcStopAside): the stop aside of a still NPC or object at (x, y), its height set on the server's floor (within
// SIDE_STOP_RISE yards of the NPC's), is walkable: no wall in between from the NPC, not in a booth, no avoided spot, a
// path to it the core follows, in the NPC's sight. False: the caller stops as before.
bool AsideStopOk(Player* bot, WorldObject const* npc, float& x, float& y, float& z);
}  // namespace CoaSafeMove

#endif
