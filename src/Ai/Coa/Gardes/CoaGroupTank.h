/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GROUP_TANK_H
#define COA_GROUP_TANK_H

#include "ObjectGuid.h"

#include <functional>
#include <string>

class Player;

// The group's reference tank, read by the combat guards that came from Dungeon Clear (healer placement, threat lead,
// assist on the tank's fight; 07/11, "DC is the autorun in the dungeon, the strategies are in playerbots"):
//  1. the run leader of an active Dungeon Clear run, from the provider Dungeon Clear sets when it loads (the module
//     never depends on Dungeon Clear);
//  2. else the player who tanks for the group ("tank main on", CoaPlayerMainTank);
//  3. else the member carrying the group's main tank flag;
//  4. else the first living tank of the group.
// Only members on the member's map count; nullptr without a group or a tank.
namespace CoaGroupTank
{
    // Returns the run leader for this member while a run is active, else nullptr. Set once, at load, before the maps
    // update; never changed afterwards.
    using Provider = std::function<Player*(Player const* member)>;
    void SetProvider(Provider provider);

    // What the provider answered for one member: whether a run is active for him, and its leader. A bot reads it
    // through its "coa run leader" value (cached for its tick: the provider takes Dungeon Clear's leader-cache lock,
    // and the guards ask it several times per action candidate); a human member asks the provider directly.
    struct RunLeaderMemo
    {
        bool inRun = false;
        ObjectGuid leader;
    };
    // The provider's answer, read now (the value's Calculate). Nothing in a run when no provider is set.
    RunLeaderMemo ReadRunLeader(Player const* member);

    Player* Reference(Player const* member);

    // Whether this member is in an active Dungeon Clear run (the provider gives it a run leader).
    bool InDungeonClearRun(Player const* member);

    // A guard that is always on inside a Dungeon Clear run and behind `keyOn` outside one. With
    // AiPlayerbot.CoaCombat.BenchKeyOnly (bench only), the key alone decides, inside a run too.
    bool RunOrKey(Player const* member, bool keyOn);

    // The guards Dungeon Clear still runs itself during its runs (by name, e.g. "heal reposition"): the module's guard
    // of the same name stands down for the members of an active run, so that no guard acts twice. Dungeon Clear
    // declares them at load, and drops a name in the same change that cuts its own guard. Set before the maps update.
    void SetHandledByDungeonClear(std::string const& guard, bool handled);
    // The guards' scope (2.9.4): instances only. True when the member is in a group and on a dungeon or raid map
    // (Map::IsDungeon; battlegrounds and arenas are out). Every guard (trigger, action, multiplier, value that changes
    // what a bot does) returns at once, before any lock or setting read, when this is false; the decorating values
    // ("dps target", "party member to heal", ...) give the stock value back.
    bool GuardScope(Player const* member);
    // Whether the module's guard `guard` acts for this member: never outside GuardScope; then always outside a Dungeon
    // Clear run, and inside one unless Dungeon Clear still runs that guard itself.
    bool ModuleGuardActs(Player const* member, std::string const& guard);
}

#endif
