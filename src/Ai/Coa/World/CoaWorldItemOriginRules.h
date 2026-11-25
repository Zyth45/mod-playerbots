/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of characters.coa_item_origin (no game type, tested in tests/coa_world_item_origin_rules.cpp).
// Form agreed with the dashboard: D:\CoA-Commun\FORME-ITEM-ORIGIN.md.

#ifndef PLAYERBOTS_COAWORLDITEMORIGINRULES_H
#define PLAYERBOTS_COAWORLDITEMORIGINRULES_H

#include <cstdint>
#include <map>
#include <string>

namespace CoaWorldItemOriginRules
{
// A piece the armory shows: worn in one of the 19 equipment slots, at least minQuality (2: green). Not a bag (18),
// ammunition (24) nor a quiver (27), and not a non-equippable item (0).
inline bool ArmoryPiece(uint32_t quality, uint32_t inventoryType, uint32_t minQuality)
{
    return quality >= minQuality && inventoryType != 0 && inventoryType != 18 && inventoryType != 24 &&
           inventoryType != 27;
}

// The source of a piece worn: the origin noted when it arrived; else "start" when the factory or the auto-gear put it
// on (inside a CoaWorld::StartGearScope); else none (nullptr: no row, the armory shows "Origin unknown"). A piece its
// wearer made keeps no row (the dashboard reads the creator). Never a guessed origin.
inline char const* WornSource(char const* noted, bool startScope, bool madeByWearer)
{
    if (noted)
        return noted;
    return startScope && !madeByWearer ? "start" : nullptr;
}

// The level band of the hourly "gear" line: 1-9, 10-19, ..., 80.
inline uint32_t LevelBand(uint32_t level) { return level / 10 * 10; }

// Rows older than purgeDays go (0: never).
inline int64_t PurgeBefore(int64_t now, uint32_t purgeDays) { return purgeDays ? now - int64_t(purgeDays) * 86400 : 0; }

// The hourly line: "1-9: 3 start; 10-19: 12 loot, 2 quest", bands and sources in order; empty with nothing.
using HourCounts = std::map<uint32_t, std::map<std::string, uint32_t>>;
inline std::string HourLine(HourCounts const& counts)
{
    std::string out;
    for (auto const& [band, sources] : counts)
    {
        out += (out.empty() ? "" : "; ") + (band ? std::to_string(band) + "-" + std::to_string(band + 9)
                                                 : std::string("1-9")) + ":";
        bool first = true;
        for (auto const& [source, count] : sources)
        {
            out += std::string(first ? " " : ", ") + std::to_string(count) + " " + source;
            first = false;
        }
    }
    return out;
}
}  // namespace CoaWorldItemOriginRules

#endif
