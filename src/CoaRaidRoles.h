/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_RAID_ROLES_H
#define COA_RAID_ROLES_H

#include <algorithm>
#include <cstdint>

// The tanks and healers of a raid of `size` built with bots, when the player gave none (0): 2 tanks
// up to 25, then one in eight; healers one in four up to 10, 3 from 11 to 20, 4 from 21 to 25, then one
// in four again. Counts the player gave are kept. One seat at least stays for a damage dealer.
inline void CoaRaidRoles(std::uint32_t size, std::uint32_t& tanks, std::uint32_t& heals)
{
    if (!tanks)
        tanks = size <= 25 ? 2 : std::max<std::uint32_t>(2, size / 8);
    if (!heals)
        heals = size <= 10 ? (size + 2) / 4 : size <= 20 ? 3 : size <= 25 ? 4 : (size + 2) / 4;
    tanks = std::min(tanks, size - 1);
    heals = std::min(heals, size - 1 - tanks);
}

#endif
