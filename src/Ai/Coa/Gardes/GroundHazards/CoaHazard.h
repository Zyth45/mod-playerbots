/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_HAZARD_H
#define COA_HAZARD_H

#include "CoaHazardKernel.h"

class Player;

// The LIVE half of the ground hazards guard (Dungeon Clear's DcHazard, logic unchanged): "is this spot / this leg
// standing in a persistent damage aura?", over all three emitter kinds (creature pulses via "coa hazards", ground
// pools via "coa ground hazards", gameobject traps via "coa trap hazards"; all three 500ms-cached).
//
// MAP THREAD ONLY: these resolve live positions. Every entry point takes the cheap registry early-out first, so a map
// with no rows pays one table scan per kind and nothing else.
namespace CoaHazard
{
    // Resolve every live hazard around `bot` into one flat set, in registry order. Empty, and free, on a map with no
    // rows. A caller asking about several points in one tick samples once and uses the LiveSet forms.
    LiveSet Sample(Player* bot);

    bool PointIsHot(Player* bot, float x, float y, float z);
    bool SegmentIsHot(Player* bot, float ax, float ay, float az, float bx, float by, float bz);
    bool LegIsHot(Player* bot, float x, float y, float z);
    VacateEmitter NearestVacate(Player* bot);
    bool PointIsInVacateBand(Player* bot, float x, float y, float z);
}

#endif
