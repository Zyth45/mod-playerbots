/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAMISSIONZONERULES_H
#define _PLAYERBOT_COAMISSIONZONERULES_H

// Gathering missions are only ever given to alts, so they have their own maps (AiPlayerbot.Missions.Maps): random bots
// stay on AiPlayerbot.RandomBotMaps while an alt may be sent to Outland for Fel Iron Ore. A zone rescaled by CoA
// (coa_zone_scaling, coa_zone_creature_budget) counts at the levels its creatures are given in game, not at the level
// of their templates or of the stock Burning Crusade brackets that a level 60 realm drops. Without the game objects,
// for the unit test.

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace CoaMissionZone
{
struct Bracket
{
    uint32_t low = 0;
    uint32_t high = 0;
};

inline bool OnMissionMap(std::vector<uint32_t> const& maps, uint32_t mapId)
{
    return std::find(maps.begin(), maps.end(), mapId) != maps.end();
}

// The level band of each rescaled zone: from its lowest to its highest creature budget level.
inline std::map<uint32_t, Bracket> ScaledBrackets(std::vector<std::pair<uint32_t, uint32_t>> const& zoneLevels)
{
    std::map<uint32_t, Bracket> brackets;
    for (auto const& [zoneId, level] : zoneLevels)
    {
        if (!level)
            continue;
        auto [it, added] = brackets.try_emplace(zoneId, Bracket{level, level});
        if (!added)
        {
            it->second.low = std::min(it->second.low, level);
            it->second.high = std::max(it->second.high, level);
        }
    }
    return brackets;
}

// A rescaled zone's band wins over the stock bracket; a zone with neither is no mission zone.
template <class StockBracket>
std::optional<Bracket> ZoneBracket(std::map<uint32_t, Bracket> const& scaled,
                                   std::map<uint32_t, StockBracket> const& stock, uint32_t zoneId)
{
    if (auto const it = scaled.find(zoneId); it != scaled.end())
        return it->second;
    if (auto const it = stock.find(zoneId); it != stock.end())
        return Bracket{it->second.low, it->second.high};
    return std::nullopt;
}

// The level a creature fights at: its rescaled budget level (the highest when several zones list it), else the template.
inline uint32_t CreatureLevel(std::map<uint32_t, uint32_t> const& scaledLevels, uint32_t entry, uint32_t templateLevel)
{
    auto const it = scaledLevels.find(entry);
    return it != scaledLevels.end() && it->second ? it->second : templateLevel;
}
}  // namespace CoaMissionZone

#endif
