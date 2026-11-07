/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_IRON_HALL_DECISION_H
#define COA_IRON_HALL_DECISION_H

#include "CoaBrdData.h"

#include <cmath>
#include <cstdint>

// Blackrock Depths' Iron Hall safe spot, from Dungeon Clear (DcIronHallDecision.h), pure and unchanged: where may a
// member stop? The fire circles of the six statues meet on the centre line at every band, so the party has to walk
// through fire; it must never STOP in it. The hall is a chain of four stops:
//   * clear of the fire: nothing to do;
//   * in the fire and MOVING: let it cross, unless a follower's spline ends in the fire (redirect it). Never the
//     leader: its route crosses the bands on purpose;
//   * in the fire and STOPPED: hop.
// Which slot: in a fight, the nearest from which it still hits its target; otherwise, of the two stops either side,
// the one nearer its goal (the tank for a follower, the way on for the leader); its own slot of that stop by guid.
namespace CoaIronHall
{
    struct Spot
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    inline bool InHall(float x, float y, float z)
    {
        namespace B = CoaBrd;
        return x >= B::IRON_HALL_MIN_X && x <= B::IRON_HALL_MAX_X && y >= B::IRON_HALL_HOP_MIN_Y &&
               y <= B::IRON_HALL_HOP_MAX_Y && z >= B::IRON_HALL_HOP_MIN_Z && z <= B::IRON_HALL_HOP_MAX_Z;
    }

    inline float FireDistance(float x, float y)
    {
        float best = 1.0e9f;
        for (CoaBrd::HallPoint const& g : CoaBrd::IRONHAND_GUARDIANS)
            best = std::fmin(best, std::hypot(x - g.x, y - g.y));
        return best;
    }

    inline bool InFire(float x, float y)
    {
        return FireDistance(x, y) < CoaBrd::IRON_HALL_FIRE_KEEPOUT;
    }

    // The stop a point stands in, or -1 in the fire.
    inline int ZoneOf(float x, float y)
    {
        if (InFire(x, y))
            return -1;
        int best = 0;
        for (std::uint32_t i = 1; i < CoaBrd::IRON_HALL_ZONE_COUNT; ++i)
            if (std::fabs(CoaBrd::IRON_HALL_ZONES[i].y - y) < std::fabs(CoaBrd::IRON_HALL_ZONES[best].y - y))
                best = static_cast<int>(i);
        return best;
    }

    inline Spot SlotOf(std::uint32_t zone, std::uint32_t slot)
    {
        namespace B = CoaBrd;
        B::HallPoint const& z = B::IRON_HALL_ZONES[zone % B::IRON_HALL_ZONE_COUNT];
        B::HallPoint const& o = B::IRON_HALL_SLOT_OFFSETS[slot % B::IRON_HALL_SLOT_COUNT];
        return {z.x + o.x, z.y + o.y};
    }

    // Of the stops either side of y (north: larger y), the one nearer the goal.
    inline std::uint32_t ZoneToward(float y, float goalX, float goalY)
    {
        namespace B = CoaBrd;
        int north = -1, south = -1;
        for (std::uint32_t i = 0; i < B::IRON_HALL_ZONE_COUNT; ++i)
        {
            if (B::IRON_HALL_ZONES[i].y >= y)
                north = static_cast<int>(i);
            else if (south < 0)
                south = static_cast<int>(i);
        }
        if (north < 0)
            return static_cast<std::uint32_t>(south);
        if (south < 0)
            return static_cast<std::uint32_t>(north);
        float const dn = std::hypot(B::IRON_HALL_ZONES[north].x - goalX, B::IRON_HALL_ZONES[north].y - goalY);
        float const ds = std::hypot(B::IRON_HALL_ZONES[south].x - goalX, B::IRON_HALL_ZONES[south].y - goalY);
        return static_cast<std::uint32_t>(dn <= ds ? north : south);
    }

    struct Inputs
    {
        float x = 0.0f, y = 0.0f;
        bool moving = false;
        bool hasDest = false;
        float destX = 0.0f, destY = 0.0f;
        bool isLeader = false;
        float goalX = 0.0f, goalY = 0.0f;
        bool hasTarget = false;
        float targetX = 0.0f, targetY = 0.0f;
        float reach = 0.0f;
        std::uint32_t slotSeed = 0;
    };

    struct Verdict
    {
        bool hop = false;
        Spot spot{};
        std::uint32_t zone = 0;
        bool inReach = false;
        bool redirect = false;
    };

    inline Verdict Decide(Inputs const& in)
    {
        namespace B = CoaBrd;
        Verdict v;
        if (!InFire(in.x, in.y))
            return v;

        if (in.moving)
        {
            if (in.isLeader || !in.hasDest || !InFire(in.destX, in.destY))
                return v;
            v.redirect = true;
        }

        if (in.hasTarget)
        {
            float bestD = 1.0e9f;
            for (std::uint32_t z = 0; z < B::IRON_HALL_ZONE_COUNT; ++z)
                for (std::uint32_t s = 0; s < B::IRON_HALL_SLOT_COUNT; ++s)
                {
                    Spot const p = SlotOf(z, s);
                    if (std::hypot(p.x - in.targetX, p.y - in.targetY) > in.reach)
                        continue;
                    float const d = std::hypot(p.x - in.x, p.y - in.y);
                    if (d < bestD)
                    {
                        bestD = d;
                        v.spot = p;
                        v.zone = z;
                        v.inReach = true;
                    }
                }
            if (v.inReach)
            {
                v.hop = true;
                return v;
            }
        }

        v.zone = ZoneToward(in.y, in.goalX, in.goalY);
        v.spot = SlotOf(v.zone, in.slotSeed);
        v.hop = true;
        return v;
    }

    // Follow-tank clamp: a follower parked clear of the fire in the stop its tank holds stays parked.
    inline bool HoldFollow(float x, float y, float tankX, float tankY)
    {
        int const mine = ZoneOf(x, y);
        return mine >= 0 && mine == ZoneOf(tankX, tankY);
    }
}

#endif
