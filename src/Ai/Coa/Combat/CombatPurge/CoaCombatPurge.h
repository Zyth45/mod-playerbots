/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_COMBAT_PURGE_H
#define COA_COMBAT_PURGE_H

#include "Define.h"

class Player;

// Combat purge (CoA Combat, guard B4, from Dungeon Clear's DcCombatPurge). A creature of the hand-vetted table
// (CoaCombatPurgeRule.h) holds a party in a combat it can never end: instanced creatures never leash, and these can
// neither be killed nor evade. Once it has held a grouped bot for the window (60 s), the creature's threat and combat
// are cleared (which ends the reference for every member at once), the bots' stale targets on it are dropped, and a
// target bar on its entry is armed for the same window.
//
// It acts for the members of a Dungeon Clear run, as before, and outside a run when
// AiPlayerbot.CoaCombat.CombatPurge.Enable is 1 (0 by default). Never during a raid encounter (the boss stand-down).
namespace CoaCombatPurge
{
    // World thread: advances every bot's clock by `diffMs` and purges for the ones past the window.
    void Tick(uint32 diffMs);

    // Drop every listed holder off this member's party (same map, humans included), clear the bots' targets on them
    // and arm the bar. Returns how many creatures were dropped.
    uint32 Purge(Player* member);

    // Is `entry` under a post-purge target bar in this bot's instance right now? Any thread.
    bool IsBarred(Player* bot, uint32 entry);
}

#endif
