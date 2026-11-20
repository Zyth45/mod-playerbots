/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COASAFEMOVE_H
#define _PLAYERBOT_COASAFEMOVE_H

class Player;

namespace CoaSafeMove
{
// Moves the bot to the point along the navmesh; false, and no move, when no whole path leads there (the core would
// go in a straight line through walls). allowSwim: a straight swim is allowed when the bot is in water and sees
// the point (out of water toward the shore).
bool MovePoint(Player* bot, float x, float y, float z, bool allowSwim = false);
// True when the core would follow a path to the point, not a straight line through walls (no move made).
bool CanWalkTo(Player* bot, float x, float y, float z);
}  // namespace CoaSafeMove

#endif
