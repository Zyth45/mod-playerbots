/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GARDES_SETTINGS_H
#define COA_GARDES_SETTINGS_H

// The tunables of the party combat guards, each read as AiPlayerbot.<Key> with the player's old DungeonClear.<Key>
// first (CoaGardesConfig). Defaults are Dungeon Clear's registry defaults. The guards read them every tick, and a
// configuration lookup walks every key, so the values are read once, on the first call, into one immutable snapshot
// read without a lock afterwards (like the heal and threat lead settings). A change needs a restart.
struct CoaGardesSettings
{
    bool combatRegroup = true;          // CombatRegroup
    float combatRegroupDistance = 40.0f; // CombatRegroupDistance: the hard outer tether
    float combatRegroupSlack = 8.0f;    // CombatRegroupSlack: healer pre-position band under heal range
    float combatRegroupCooldown = 5.0f; // CombatRegroupCooldown (s)
    float healRepositionHpFloor = 90.0f; // HealRepositionHpFloor: a member under it is a hurt heal target
    float roomAggroPathPadding = 3.0f;  // RoomAggroPathPadding: the room boss wake sphere's party buffer
    float aggroRangeMargin = 2.0f;      // AggroRangeMargin

    static CoaGardesSettings const& Get();
};

#endif
