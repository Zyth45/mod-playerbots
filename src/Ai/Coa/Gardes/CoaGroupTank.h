/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_GROUP_TANK_H
#define COA_GROUP_TANK_H

#include <functional>

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
}

#endif
