// Dungeon Clear 2.9.4 (coa/2.9.4 @ 0590afb), Util/DcIronHallDecision.h - copied verbatim as the non-regression ORACLE of the
// module's party combat guards. Only the includes are adapted so it compiles alone (no core header). Never edit.
/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCIRONHALLDECISION_H
#define _PLAYERBOT_DCIRONHALLDECISION_H

#include <cmath>
#include <cstdint>



#include "Dc294BrdTables.h"

// PURE kernel for Blackrock Depths' Iron Hall (map 230): where may a member stop?
//
// Six Ironhand Guardians line the hall in three facing pairs, each pulsing Gout
// of Flame (15529 -> 15538, a 30yd circle, ~180 fire a second) — under
// mod-individual-progression forever, in or out of combat. The circles meet on
// the centre line at every band, so there is no fire-free route through: the
// party has to WALK through fire. What it must never do is STOP in it, and
// nothing hazard-aware decided where a bot stops here — follow, regroup, heal
// reposition and stock combat movement all ignore DcHazard, the generic vacate is
// off (a radial retreat would only fight the walk), and the hall is a
// fight-in-place zone so no camp is ever planned.
//
// So the hall is a chain of four stops (DcBlackrockDepths::IRON_HALL_ZONES) and
// this decides, for one member, whether it has to hop to one now and which slot:
//
//   * Clear of the fire: nothing to do.
//   * In the fire and MOVING: let it cross. The one exception is a follower
//     whose spline ends in the fire — redirect it before it gets there. Never
//     the leader: its route spline crosses the bands on purpose, and the driver
//     owns it.
//   * In the fire and STOPPED (or about to be): hop.
//
// Which slot:
//   1. In a fight, the nearest slot from which it can still hit its target —
//      Magmus stands in gap 2, so melee and casters both keep swinging from it.
//   2. Otherwise, of the two stops either side of it, the one nearer its GOAL:
//      the tank for a follower, the way on (Magmus, then the throne door) for
//      the leader. A follower whose tank is two stops away hops one stop
//      forward, and its follow carries it the rest — stop to stop.
//   3. Its own slot of that stop (by guid), so the party spreads over five.
namespace DcIronHall
{
    struct Spot
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    inline bool InHall(float x, float y, float z)
    {
        namespace B = DcBlackrockDepths;
        return x >= B::IRON_HALL_MIN_X && x <= B::IRON_HALL_MAX_X &&
               y >= B::IRON_HALL_HOP_MIN_Y && y <= B::IRON_HALL_HOP_MAX_Y &&
               z >= B::IRON_HALL_HOP_MIN_Z && z <= B::IRON_HALL_HOP_MAX_Z;
    }

    // Distance to the nearest statue, 2D.
    inline float FireDistance(float x, float y)
    {
        float best = 1.0e9f;
        for (DcBlackrockDepths::HallPoint const& g : DcBlackrockDepths::IRONHAND_GUARDIANS)
            best = std::fmin(best, std::hypot(x - g.x, y - g.y));
        return best;
    }

    inline bool InFire(float x, float y)
    {
        return FireDistance(x, y) < DcBlackrockDepths::IRON_HALL_FIRE_KEEPOUT;
    }

    // The stop a point stands in, or -1 when it is in the fire. Every fire-free
    // point of the hall lies in one of the four gaps, so the nearest stop by y is
    // the one it is in.
    inline int ZoneOf(float x, float y)
    {
        if (InFire(x, y))
            return -1;
        int best = 0;
        for (uint32 i = 1; i < DcBlackrockDepths::IRON_HALL_ZONE_COUNT; ++i)
            if (std::fabs(DcBlackrockDepths::IRON_HALL_ZONES[i].y - y) <
                std::fabs(DcBlackrockDepths::IRON_HALL_ZONES[best].y - y))
                best = static_cast<int>(i);
        return best;
    }

    inline Spot SlotOf(uint32 zone, uint32 slot)
    {
        namespace B = DcBlackrockDepths;
        B::HallPoint const& z = B::IRON_HALL_ZONES[zone % B::IRON_HALL_ZONE_COUNT];
        B::HallPoint const& o = B::IRON_HALL_SLOT_OFFSETS[slot % B::IRON_HALL_SLOT_COUNT];
        return { z.x + o.x, z.y + o.y };
    }

    // Of the stops either side of y (north: larger y), the one nearer the goal.
    // Past either end of the chain there is only one side.
    inline uint32 ZoneToward(float y, float goalX, float goalY)
    {
        namespace B = DcBlackrockDepths;
        int north = -1, south = -1;
        for (uint32 i = 0; i < B::IRON_HALL_ZONE_COUNT; ++i)
        {
            if (B::IRON_HALL_ZONES[i].y >= y)
                north = static_cast<int>(i);  // zones run north -> south
            else if (south < 0)
                south = static_cast<int>(i);
        }
        if (north < 0)
            return static_cast<uint32>(south);
        if (south < 0)
            return static_cast<uint32>(north);
        float const dn = std::hypot(B::IRON_HALL_ZONES[north].x - goalX, B::IRON_HALL_ZONES[north].y - goalY);
        float const ds = std::hypot(B::IRON_HALL_ZONES[south].x - goalX, B::IRON_HALL_ZONES[south].y - goalY);
        return static_cast<uint32>(dn <= ds ? north : south);
    }

    struct Inputs
    {
        float x = 0.0f, y = 0.0f;   // the member
        bool  moving = false;
        bool  hasDest = false;      // the end of its current spline, when moving
        float destX = 0.0f, destY = 0.0f;
        bool  isLeader = false;
        float goalX = 0.0f, goalY = 0.0f;  // the tank (follower) / the way on (leader)
        bool  hasTarget = false;    // in a fight with a live target
        float targetX = 0.0f, targetY = 0.0f;
        float reach = 0.0f;         // centre-to-centre distance it can hit the target from
        uint32 slotSeed = 0;        // the bot's guid counter
    };

    struct Verdict
    {
        bool hop = false;
        Spot spot{};
        uint32 zone = 0;
        bool inReach = false;       // chosen because it can hit its target from there
        bool redirect = false;      // it was moving toward the fire, not stopped in it
    };

    inline Verdict Decide(Inputs const& in)
    {
        namespace B = DcBlackrockDepths;
        Verdict v;
        if (!InFire(in.x, in.y))
            return v;

        if (in.moving)
        {
            if (in.isLeader || !in.hasDest || !InFire(in.destX, in.destY))
                return v;  // crossing — keep going
            v.redirect = true;
        }

        if (in.hasTarget)
        {
            float bestD = 1.0e9f;
            for (uint32 z = 0; z < B::IRON_HALL_ZONE_COUNT; ++z)
                for (uint32 s = 0; s < B::IRON_HALL_SLOT_COUNT; ++s)
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

    // Follow-tank clamp: a follower parked clear of the fire in the stop its tank
    // holds stays parked. Follow installs a persistent MoveFollow toward a
    // golden-angle slot around the tank, and in a gap 8yd wide that slot is as
    // likely as not in the fire — so without this the hop and the follow would
    // trade the bot back and forth for as long as the tank stood there.
    inline bool HoldFollow(float x, float y, float tankX, float tankY)
    {
        int const mine = ZoneOf(x, y);
        return mine >= 0 && mine == ZoneOf(tankX, tankY);
    }
}

#endif  // _PLAYERBOT_DCIRONHALLDECISION_H
