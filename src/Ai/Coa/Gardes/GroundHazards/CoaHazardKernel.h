/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_HAZARD_KERNEL_H
#define COA_HAZARD_KERNEL_H

#include "CoaHazardRegistry.h"

#include <cstdint>
#include <limits>
#include <vector>

// The pure half of the ground hazards guard (Dungeon Clear's DcHazard LiveSet forms, logic unchanged): a sample of
// the live hazards around a bot, and the four questions asked of it. No game state, so the standalone tests can pin
// it. The game half (resolving creatures, dynamic objects and traps into a LiveSet) lives in CoaHazard.cpp.
namespace CoaHazard
{
    // The defaults a row inherits when it does not say otherwise (see CoaHazardRegistry.h for the two bands).
    inline constexpr float VacateRetreatSlack = 6.0f;
    inline constexpr float VacateStayBand = 2.0f;

    // The retreat's own tuning (DungeonClearTuning.h, unchanged): a committed retreat point is ridden at most this
    // long, and a candidate's path may be no longer than max(straight * ratio, straight + slack).
    inline constexpr std::uint32_t VacateCommitMaxMs = 4000;
    inline constexpr float VacateDetourRatio = 1.5f;
    inline constexpr float VacateDetourSlack = 8.0f;

    constexpr float DetourBound(float straight, float ratio, float slack)
    {
        float const byRatio = straight * ratio;
        float const bySlack = straight + slack;
        return byRatio > bySlack ? byRatio : bySlack;
    }

    struct VacateEmitter
    {
        bool ok{false};
        float x{0.0f}, y{0.0f}, z{0.0f};
        float pulseRadius{0.0f};
        float retreatSlack{VacateRetreatSlack};  // this row's, not the global
    };

    struct LiveHazard
    {
        float x{0.0f}, y{0.0f}, z{0.0f};

        // Placement keep-out: what PointIsHot / SegmentIsHot judge against.
        float radius{0.0f};
        float zBand{0.0f};

        // Active-vacate band: what NearestVacate / PointIsInVacateBand judge against. 0 = not a vacate row.
        float vacateRadius{0.0f};
        float holdBand{0.0f};
        float retreatSlack{VacateRetreatSlack};

        // Creature liveness at sample time (a dead emitter has stopped pulsing). Pools and traps sample as alive:
        // resolving their guid is the liveness check.
        bool alive{true};
    };

    // In registry order (creature emitters, then ground pools, then traps), so NearestVacate's nearest-wins
    // tie-break keeps the first strictly nearest centre.
    using LiveSet = std::vector<LiveHazard>;

    // Is (px,py,pz) inside `h`'s DANGER band? Same floor, within the row's pulse + holdBand. Non-vacate rows and dead
    // creature emitters are never in danger.
    inline bool InDangerBand(LiveHazard const& h, float px, float py, float pz)
    {
        if (h.vacateRadius <= 0.0f || !h.alive)
            return false;

        if (std::fabs(h.z - pz) > h.zBand)
            return false;

        float const reach = h.vacateRadius + h.holdBand;
        float const dx = px - h.x, dy = py - h.y;
        return dx * dx + dy * dy <= reach * reach;
    }

    // True when (x,y,z) lies inside any live hazard's placement keep-out: the test for a point the party will STAND on.
    inline bool PointIsHot(LiveSet const& live, float x, float y, float z)
    {
        for (LiveHazard const& h : live)
            if (CoaHazardRegistry::PointInCylinder(h.radius, h.zBand, h.x, h.y, h.z, x, y, z))
                return true;
        return false;
    }

    // True when the straight segment clips any live hazard's keep-out: the test for a leg the party will WALK ALONG.
    inline bool SegmentIsHot(LiveSet const& live, float ax, float ay, float az, float bx, float by, float bz)
    {
        for (LiveHazard const& h : live)
            if (CoaHazardRegistry::SegmentClipsCylinder(h.radius, h.zBand, h.x, h.y, h.z, ax, ay, az, bx, by, bz))
                return true;
        return false;
    }

    // True when (x,y,z) lies inside ANY live vacate band: what a retreat must validate its destination against, so
    // that the trigger does not fire again the moment the bot arrives (not the same question as PointIsHot).
    inline bool PointIsInVacateBand(LiveSet const& live, float x, float y, float z)
    {
        for (LiveHazard const& h : live)
            if (InDangerBand(h, x, y, z))
                return true;
        return false;
    }

    // The nearest live vacate hazard whose danger band contains (px,py,pz); ok == false when clear of all of them.
    inline VacateEmitter NearestVacate(LiveSet const& live, float px, float py, float pz)
    {
        VacateEmitter best;
        float bestDistSq = std::numeric_limits<float>::max();

        for (LiveHazard const& h : live)
        {
            if (!InDangerBand(h, px, py, pz))
                continue;

            float const dx = px - h.x, dy = py - h.y;
            float const distSq = dx * dx + dy * dy;
            if (distSq >= bestDistSq)
                continue;

            bestDistSq = distSq;
            best.ok = true;
            best.x = h.x;
            best.y = h.y;
            best.z = h.z;
            best.pulseRadius = h.vacateRadius;
            best.retreatSlack = h.retreatSlack;
        }

        return best;
    }

    // Row -> LiveHazard, as Sample builds them (one helper so the three kinds cannot drift apart).
    template <class Row>
    inline LiveHazard FromRow(Row const& r, float x, float y, float z, bool alive)
    {
        return LiveHazard{x, y, z, r.radius, r.zBand, r.vacateRadius, r.holdBand, r.retreatSlack, alive};
    }
}

#endif
