/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_HEAL_ESCORT_H
#define COA_HEAL_ESCORT_H

#include <functional>

class Player;
class Unit;

// The creature a group escorts and its healers keep alive like a member (Old Hillsbrad's Thrall, the Disciple of
// Wailing Caverns): Dungeon Clear knows it while its run escorts one and gives it through a provider set once, at
// load, before the maps update; the module never depends on Dungeon Clear. Read by `party member to heal`
// (CoaPartyMemberToHealValue) and `coa heal target`. Without a provider, or off an escort, nothing changes.
namespace CoaHealEscort
{
    // Returns the escorted creature for this member, else nullptr.
    using Provider = std::function<Unit*(Player const* member)>;
    void SetProvider(Provider provider);

    // The living escorted creature on the member's map, else nullptr.
    Unit* Escortee(Player const* member);
}

#endif
