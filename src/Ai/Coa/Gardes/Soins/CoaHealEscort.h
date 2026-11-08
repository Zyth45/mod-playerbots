/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
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
