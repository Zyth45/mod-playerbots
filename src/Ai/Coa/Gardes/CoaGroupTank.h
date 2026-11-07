/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GROUP_TANK_H
#define COA_GROUP_TANK_H

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

    Player* Reference(Player const* member);

    // Whether this member is in an active Dungeon Clear run (the provider gives it a run leader).
    bool InDungeonClearRun(Player const* member);

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
