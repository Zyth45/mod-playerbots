/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASTRANDEDRULES_H
#define PLAYERBOTS_COASTRANDEDRULES_H

#include <cstdint>

// AiPlayerbot.CoaLive.StrandedMinutes (3.3.1, a player's report: a random bot still for 1h18m at Blackrock Mountain,
// left in a group of bots by a player gone): the last resort, whatever the cause. The orphan release of the groups
// (AiPlayerbot.CoaOrphanGroupReleaseDelay) comes first.
namespace CoaStrandedRules
{
// Moved: more than 3 yards from where it stood (a turn on the spot, a sit, a step back do not count).
inline bool Moved(float dx, float dy, float dz) { return dx * dx + dy * dy + dz * dz > 9.0f; }

enum class Release
{
    None,
    LeaveGroup,  // out of its group, no master, default strategies, off its mount
    NextRpg      // out of a group: its RPG picks its next goal, nothing else changed
};

// A random bot out of combat, alive, out of a battleground, a queue or a test, still for limitMinutes (0: off):
// - in a group with no real player in it: LeaveGroup (the group of a player gone, a party of bots stuck);
// - out of a group, its RPG idle or resting: NextRpg, unless the stillness is wanted (wantedStill: sitting, resting
//   in an inn or a city, away, casting or channelling - fishing, a craft -, in a capital, which CityUnstick watches).
inline Release Stranded(std::uint32_t stillMs, std::uint32_t limitMinutes, bool grouped, bool realPlayerInGroup,
                        bool idleRpg, bool wantedStill)
{
    if (!limitMinutes || stillMs < limitMinutes * 60000u)
        return Release::None;
    if (grouped)
        return realPlayerInGroup ? Release::None : Release::LeaveGroup;
    return idleRpg && !wantedStill ? Release::NextRpg : Release::None;
}
}  // namespace CoaStrandedRules

#endif
