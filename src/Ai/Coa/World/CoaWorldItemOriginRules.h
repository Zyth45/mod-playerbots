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

// The source of a piece worn. A piece with a known maker (the creator field, or the crafter noted) is "selfcraft" on
// its maker and "craft" on anyone else, however it came (3.3.1: a crafted piece traded or mailed stays a craft).
// Else the last hand-over seen ("trade", "guildbank": moved) beats the origin noted when it was made or looted;
// else the noted origin; else "start" when the factory or the auto-gear put it on (inside a
// CoaWorld::StartGearScope); else none (nullptr: no row, the armory shows "Origin unknown"). Never a guessed origin.
inline char const* WornSource(char const* moved, char const* noted, bool startScope, bool hasMaker, bool madeByWearer)
{
    if (hasMaker)
        return madeByWearer ? "selfcraft" : "craft";
    if (moved)
        return moved;
    if (noted)
        return noted;
    return startScope ? "start" : nullptr;
}

// characters.coa_item_event: the last Keep events of each character (ItemEvent.Keep, 10 to 200, default 20).
constexpr uint32_t EventsKept = 20;
inline uint32_t KeepEvents(uint32_t configured) { return configured < 10 ? 10 : configured > 200 ? 200 : configured; }

// The same piece worn again within a minute of its last "equip" event (a swap back, a reload of the gear): no new
// event. lastAt 0: never seen.
inline bool RepeatPose(uint32_t lastAt, uint32_t now) { return lastAt && now >= lastAt && now - lastAt < 60; }

// No new "equip" event either for the piece its wearer put on last (3.3.1 bench: a bot swapping a white weapon and its
// Skinning Knife wrote the knife 7 times an hour): nothing changed among its green-or-better pieces.
inline bool SkipPose(bool lastOfWearer, uint32_t lastAt, uint32_t now) { return lastOfWearer || RepeatPose(lastAt, now); }

// A pass of the factory or the auto-gear ("regear", one row): a bot's pieces worn under a StartGearScope and its
// "autogear" enchantments, until PassGap seconds go by with none (the enchantments put on right after the gear count
// in the same pass). all: the server stops.
constexpr uint32_t PassGap = 5;
inline bool PassClosed(uint32_t last, uint32_t now, bool all) { return all || now >= last + PassGap; }

// An "enchant" event: an enchantment put on, different from the one the slot had (the auto-gear re-applying the same
// one writes nothing).
inline bool NewEnchant(uint32_t before, uint32_t after) { return after && after != before; }

// The source of a looted piece (a creature, an object, a chest, fishing, a group roll, the master looter): "raid" in
// a raid map, "dungeon" in a dungeon map, else "loot" (battlegrounds and arenas too). A simulated dungeon's piece
// stays "dungeonsim".
inline char const* LootSource(bool inDungeon, bool inRaid) { return inRaid ? "raid" : inDungeon ? "dungeon" : "loot"; }

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
