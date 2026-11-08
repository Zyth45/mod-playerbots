/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_TANK_FIGHT_CLOCK_H
#define COA_TANK_FIGHT_CLOCK_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;

// The fight clock of every player who can be a group's reference tank (CoaGroupTank): when his current fight began and
// when he first damaged a creature in it (Dungeon Clear's leaderCombatSinceMs / leaderFirstHitMs and DcPlayerLed's
// stamps, 07/11). Written by the module's hooks (enter combat, damage), which run on the tank's map thread; read by
// the bots of his group on the same map. Only guids and times are kept, behind one lock: no game object of another
// map is ever touched. An entry lives from the tank's enter-combat edge to his logout.
namespace CoaTankFightClock
{
    // Whether this player can be a reference tank (a tank, the group's main tank, or a player tanking with the
    // "tank main on" bar): only those are stamped, so the hooks cost nothing for anyone else.
    bool CanLead(Player const* player);

    // The hooks: the 0->1 combat edge starts a fight, the first damage on a creature after it is the first hit.
    void NoteEnterCombat(Player const* player);
    void NoteHit(Player const* player);
    void Forget(ObjectGuid guid);

    // The tank's current fight (0 = none). Called from the tank's map thread only (a member of his group on his map):
    // out of combat, the fight is closed here.
    void Stamps(Player const* tank, uint32& combatSinceMs, uint32& firstHitMs);

    // "The tank's fight is on", latched for `graceMs` after the last positive read (CoaThreatLeadMath::
    // EngagedLatched), kept per tank.
    bool EngagedLatched(Player const* tank, bool live, uint32 graceMs);
}

#endif
