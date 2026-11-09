/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COACLEANWORLDRULES_H
#define PLAYERBOTS_COACLEANWORLDRULES_H

#include <cmath>
#include <cstdint>
#include <vector>

// A clean world (CoA Live, AiPlayerbot.CoaLive.CleanWorld.Enable): a random bot does not come back to life in the
// middle of the monsters that killed it, and does not swim about for ever. Pure rules.
namespace CoaCleanWorld
{
struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

constexpr float RECLAIM_RADIUS = 30.0f;  // a ghost revives where it stands, within 39 yd of its body
constexpr float DANGER_RADIUS = 15.0f;   // a monster this close attacks the bot as soon as it stands up
constexpr float SAFE_RADIUS = 20.0f;     // a spot with no monster this close is safe enough

inline float Dist(Point a, Point b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

inline uint32_t Near(Point at, std::vector<Point> const& hostiles, float radius)
{
    uint32_t n = 0;
    for (Point const& h : hostiles)
        if (Dist(at, h) <= radius)
            ++n;
    return n;
}

// Reviving here would start a fight at once: a monster within DANGER_RADIUS.
inline bool Unsafe(Point at, std::vector<Point> const& hostiles) { return Near(at, hostiles, DANGER_RADIUS) > 0; }

// The spots tried around the body: eight directions at RECLAIM_RADIUS.
inline std::vector<Point> Around(Point body)
{
    std::vector<Point> out;
    for (int i = 0; i < 8; ++i)
    {
        float const a = float(i) * 3.14159265f / 4.0f;
        out.push_back({body.x + RECLAIM_RADIUS * std::cos(a), body.y + RECLAIM_RADIUS * std::sin(a)});
    }
    return out;
}

// The spot to revive at: the one with no monster within SAFE_RADIUS and the farthest from the nearest monster;
// -1 when none is safe (the spirit healer then).
inline int PickSafeSpot(std::vector<Point> const& spots, std::vector<Point> const& hostiles)
{
    int best = -1;
    float bestGap = -1.0f;
    for (size_t i = 0; i < spots.size(); ++i)
    {
        float gap = 1e9f;
        for (Point const& h : hostiles)
            gap = std::fmin(gap, Dist(spots[i], h));
        if (gap > SAFE_RADIUS && gap > bestGap)
        {
            bestGap = gap;
            best = int(i);
        }
    }
    return best;
}

constexpr uint32_t SWIM_LIMIT_S = 30;  // idle in the water this long: the bot heads for the shore

inline bool LeavesWater(bool inWater, bool idle, bool inCombat, uint32_t swimmingS)
{
    return inWater && idle && !inCombat && swimmingS >= SWIM_LIMIT_S;
}
}  // namespace CoaCleanWorld

#endif
