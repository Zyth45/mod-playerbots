/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_REAL_PLAYER_SIGHT_H
#define COA_REAL_PLAYER_SIGHT_H

class Player;

// Whether a real player's client has this bot in view (Player::HaveAtClient). Used by AiPlayerbot.FlyOnlyOutOfSight
// and AiPlayerbot.TeleportOnlyOutOfSight. Walking the map's players costs one pass per call, so the answer is kept
// per bot for 1.5 s (a cache per map thread, no lock).
namespace CoaRealPlayerSight
{
bool SeenByRealPlayer(Player* bot);
}

#endif
