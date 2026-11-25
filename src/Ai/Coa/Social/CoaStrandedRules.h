/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASTRANDEDRULES_H
#define PLAYERBOTS_COASTRANDEDRULES_H

#include <cstdint>

// AiPlayerbot.CoaLive.StrandedMinutes (3.3.1, a player's report: a random bot still for 1h18m at Blackrock Mountain,
// left in a group of bots by a player gone): the safety net, whatever the cause.
namespace CoaStrandedRules
{
// Moved: more than 3 yards from where it stood (a turn on the spot, a sit, a step back do not count).
inline bool Moved(float dx, float dy, float dz) { return dx * dx + dy * dy + dz * dz > 9.0f; }

// A random bot out of combat, alive, out of a battleground, a queue or a test, still for limitMinutes (0: off):
// - in a group with no real player in it: stranded (the group of a player gone, a party of bots stuck);
// - out of a group: stranded only when its RPG is idle or resting (a bot fishing, sitting on a square, at a vendor
//   or on its way somewhere is left alone).
inline bool Stranded(std::uint32_t stillMs, std::uint32_t limitMinutes, bool grouped, bool realPlayerInGroup,
                     bool idleRpg)
{
    if (!limitMinutes || stillMs < limitMinutes * 60000u)
        return false;
    return grouped ? !realPlayerInGroup : idleRpg;
}
}  // namespace CoaStrandedRules

#endif
