/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of the simulated dungeons (no game type, tested in tests/coa_world_dungeon_sim_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDDUNGEONSIMRULES_H
#define PLAYERBOTS_COAWORLDDUNGEONSIMRULES_H

#include <algorithm>
#include <cstdint>

namespace CoaWorldDungeonSimRules
{
constexpr uint32_t BOTS_PER_GROUP = 250;  // 3.2.3: one simulated group a side for every 250 bots online
constexpr uint32_t MAX_GROUPS = 10;

// Runs a side may have going at once: the configured count (1 to 10), or with 0 one for every 250 bots online, at
// least 1 (500 bots = 2, 1 000 = 4, 2 000 = 8).
inline uint32_t GroupsPerFaction(uint32_t configured, uint32_t botsOnline)
{
    if (configured)
        return std::min(configured, MAX_GROUPS);
    return std::clamp<uint32_t>(botsOnline / BOTS_PER_GROUP, 1, MAX_GROUPS);
}
}  // namespace CoaWorldDungeonSimRules

#endif
