/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAMISSIONPOINTRULES_H
#define _PLAYERBOT_COAMISSIONPOINTRULES_H

// A gathering mission walks from spawn point to spawn point of its spot. A point it cannot reach (in a cave with no
// path, up a ledge) left it standing still until the spot was given up, 10 minutes later (Redridge Mountains, 3.0.1).
// Now a point it gets no closer to by GAIN_YD within STUCK_MS is skipped, as an empty one. Without the game objects,
// for the unit test.

#include <cstdint>

namespace CoaMissionPoint
{
constexpr float GAIN_YD = 5.0f;
constexpr uint32_t STUCK_MS = 60000;

struct Progress
{
    uint32_t sinceMs = 0;  // 0: not tracking (a new point)
    float bestDist = 0.0f;
};

// The walk to the current point, at `dist` from it: true when the point is to be skipped.
inline bool Stuck(Progress& p, float dist, uint32_t nowMs)
{
    if (!p.sinceMs || dist + GAIN_YD <= p.bestDist)
    {
        p.sinceMs = nowMs ? nowMs : 1;
        p.bestDist = dist;
        return false;
    }
    return nowMs - p.sinceMs >= STUCK_MS;
}
}  // namespace CoaMissionPoint

#endif
