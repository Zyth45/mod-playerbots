/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Dungeon Clear 2.9.4 (coa/2.9.4 @ 0590afb), Util/DcLyceumCloseUpDecision.h - copied verbatim as the non-regression ORACLE of the
// module's party combat guards. Only the includes are adapted so it compiles alone (no core header). Never edit.
/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCLYCEUMCLOSEUPDECISION_H
#define _PLAYERBOT_DCLYCEUMCLOSEUPDECISION_H

#include <cmath>

#include "Dc294BrdTables.h"

// PURE kernel for Blackrock Depths' Lyceum CLOSE-UP: in a fight inside the room,
// every member stands within LEASH of the tank — how a real group plays a room of
// 232 Anvilrage Reservists that refill a minute after they die. The tank holds the
// adds; nobody stands off at max range, nobody chases a stray across the floor.
//
// Two halves, one radius:
//   * Decide — a member further than LEASH from the tank walks back to CLOSE_TO of
//     it (DungeonClearLyceumCloseUp{Trigger,Action}, combat engine, above every
//     stock combat mover). The two radii are the hysteresis: a member is not
//     re-walked for a step's drift off the edge.
//   * ChaseLeavesThePack — stock reach / chase toward a target further than LEASH
//     from the tank is zeroed (DungeonClearCombatMultiplier). Without it a melee
//     whose target is a stray 25yd out ping-pongs between the stray and the tank;
//     with it, the stray comes to the pack, which is where the tank picks it up.
//
// The pack leash (DcPackLeashDecision.h) is the BETWEEN-fights half of the same
// rule and stays 22yd: walking members trail further than fighting ones stand.
//
// Live, tp-20261018-211235-1: inside the events, 604 Reservists joined a fight on
// someone other than the tank — 278 on DPS, 92 on the healer, 234 on pets.
namespace DcLyceumCloseUp
{
    constexpr float LEASH = 15.0f;
    constexpr float CLOSE_TO = 9.0f;
    // The walk aims inside CLOSE_TO, so arriving reads "closed up" at once.
    constexpr float AIM = CLOSE_TO - 2.0f;

    inline bool InRoom(float x, float y, float z)
    {
        using namespace DcBlackrockDepths;
        return x >= LYCEUM_BOX_X_MIN && x <= LYCEUM_BOX_X_MAX &&
               y >= LYCEUM_BOX_Y_MIN && y <= LYCEUM_BOX_Y_MAX &&
               z >= LYCEUM_BOX_Z_MIN && z <= LYCEUM_BOX_Z_MAX;
    }

    inline float Dist2d(float ax, float ay, float bx, float by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    struct Verdict
    {
        bool close = false;   // walk to (x, y)
        float x = 0.0f;
        float y = 0.0f;
    };

    // `closing`: the member is already walking back (stored latch). It keeps
    // walking until it is inside CLOSE_TO; a member at rest starts only past LEASH.
    // The aim point is on the member's own side of the tank, so a healer behind
    // the tank stays behind it rather than walking through the adds.
    inline Verdict Decide(float botX, float botY, float tankX, float tankY, bool closing)
    {
        Verdict v;
        float const d = Dist2d(botX, botY, tankX, tankY);
        if (d <= (closing ? CLOSE_TO : LEASH) || d <= 0.01f)
            return v;
        v.close = true;
        v.x = tankX + (botX - tankX) / d * AIM;
        v.y = tankY + (botY - tankY) / d * AIM;
        return v;
    }

    inline bool ChaseLeavesThePack(float targetX, float targetY, float tankX, float tankY)
    {
        return Dist2d(targetX, targetY, tankX, tankY) > LEASH;
    }
}

#endif  // _PLAYERBOT_DCLYCEUMCLOSEUPDECISION_H
