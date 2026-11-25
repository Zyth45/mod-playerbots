/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LEVELUP_TELEPORT_RULES_H
#define COA_LEVELUP_TELEPORT_RULES_H

#include <cstdint>

// The random bots' teleports (at a level up, at a refresh), apart from the game objects, so that
// tests/coa_levelup_teleport_rules.cpp can check them. AiPlayerbot.CoaLive.LevelupTeleportGuard.

// 3.3 (bench 2 of 3.3: 804 teleports an hour per 1,000 bots in the open world, 98% by bots under level 10 at their
// level up): with the new RPG, the teleport hubs of levels 1 to 5 are the races' starting points, where such a bot
// already is. Under this level, a level up teleports nowhere.
constexpr std::uint8_t COA_LEVELUP_TELEPORT_MIN_LEVEL = 6;
// A random teleport's arrival is refused when a real player stands within this many yards of it (the departure has
// the same range, RandomPlayerbotMgr::RandomTeleport).
constexpr float COA_TELEPORT_ARRIVAL_SEEN_RANGE = 150.0f;

// True: the level up teleports the bot (AutoTeleportForLevel) as before.
inline bool CoaLevelupTeleports(bool guardOn, std::uint32_t level)
{
    return !guardOn || level >= COA_LEVELUP_TELEPORT_MIN_LEVEL;
}

// True: the arrival is refused (the next location is tried), a real player being sqDist (squared yards, same map)
// from it.
inline bool CoaTeleportArrivalSeen(bool guardOn, float sqDist)
{
    return guardOn && sqDist < COA_TELEPORT_ARRIVAL_SEEN_RANGE * COA_TELEPORT_ARRIVAL_SEEN_RANGE;
}

#endif
