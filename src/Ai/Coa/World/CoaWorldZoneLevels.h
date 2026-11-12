/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_WORLD_ZONE_LEVELS_H
#define COA_WORLD_ZONE_LEVELS_H

#include <cstdint>

// The levelling zones of Kalimdor and the Eastern Kingdoms, with their level range and the side whose players level
// there, apart from the game objects so that tests/coa_world_zone_levels.cpp can check them. Zone ids from the CoA
// client's AreaTable (the classic ids; Westfall and Elwynn Alliance, the Barrens and Silverpine
// Horde, as their FactionGroupMask says). Capitals and zones not listed have no range: nothing is decided there.
// Used by CoA World: the levelling routes (where a random bot goes for its level), the density (a bot counts as near
// a player only in a zone of its level) and the recovery (a bot far above a zone does not walk around in it).

namespace CoaWorldZones
{
    enum Side : std::uint8_t
    {
        SIDE_ALLIANCE = 1,
        SIDE_HORDE = 2,
        SIDE_BOTH = 3,
    };

    struct ZoneLevels
    {
        std::uint32_t zoneId;
        std::uint32_t mapId;
        std::uint8_t minLevel;
        std::uint8_t maxLevel;
        std::uint8_t sides;
    };

    inline constexpr ZoneLevels ZONES[] = {
        // Starting zones (CoA starts Blood Elves in Tirisfal and Draenei in Teldrassil).
        { 12, 0, 1, 10, SIDE_ALLIANCE },     // Elwynn Forest
        { 1, 0, 1, 10, SIDE_ALLIANCE },      // Dun Morogh
        { 141, 1, 1, 10, SIDE_ALLIANCE },    // Teldrassil
        { 14, 1, 1, 10, SIDE_HORDE },        // Durotar
        { 215, 1, 1, 10, SIDE_HORDE },       // Mulgore
        { 85, 0, 1, 10, SIDE_HORDE },        // Tirisfal Glades
        // 10 to 20.
        { 40, 0, 10, 20, SIDE_ALLIANCE },    // Westfall
        { 38, 0, 10, 20, SIDE_ALLIANCE },    // Loch Modan
        { 148, 1, 10, 20, SIDE_ALLIANCE },   // Darkshore
        { 17, 1, 10, 25, SIDE_HORDE },       // The Barrens
        { 130, 0, 10, 20, SIDE_HORDE },      // Silverpine Forest
        // 15 to 30.
        { 44, 0, 15, 25, SIDE_ALLIANCE },    // Redridge Mountains
        { 406, 1, 15, 27, SIDE_BOTH },       // Stonetalon Mountains
        { 331, 1, 18, 30, SIDE_BOTH },       // Ashenvale
        { 10, 0, 18, 30, SIDE_ALLIANCE },    // Duskwood
        { 11, 0, 20, 30, SIDE_ALLIANCE },    // Wetlands
        { 267, 0, 20, 30, SIDE_BOTH },       // Hillsbrad Foothills
        { 400, 1, 25, 35, SIDE_BOTH },       // Thousand Needles
        // 30 to 45.
        { 36, 0, 30, 40, SIDE_BOTH },        // Alterac Mountains
        { 45, 0, 30, 40, SIDE_BOTH },        // Arathi Highlands
        { 33, 0, 30, 45, SIDE_BOTH },        // Stranglethorn Vale
        { 405, 1, 30, 40, SIDE_BOTH },       // Desolace
        { 15, 1, 35, 45, SIDE_BOTH },        // Dustwallow Marsh
        { 3, 0, 35, 45, SIDE_BOTH },         // Badlands
        { 8, 0, 35, 45, SIDE_BOTH },         // Swamp of Sorrows
        // 40 to 55.
        { 440, 1, 40, 50, SIDE_BOTH },       // Tanaris
        { 357, 1, 40, 50, SIDE_BOTH },       // Feralas
        { 47, 0, 40, 50, SIDE_BOTH },        // The Hinterlands
        { 51, 0, 43, 50, SIDE_BOTH },        // Searing Gorge
        { 16, 1, 45, 55, SIDE_BOTH },        // Azshara
        { 4, 0, 45, 55, SIDE_BOTH },         // Blasted Lands
        // 48 to 60.
        { 490, 1, 48, 55, SIDE_BOTH },       // Un'Goro Crater
        { 361, 1, 48, 55, SIDE_BOTH },       // Felwood
        { 46, 0, 50, 58, SIDE_BOTH },        // Burning Steppes
        { 28, 0, 51, 58, SIDE_BOTH },        // Western Plaguelands
        { 139, 0, 53, 60, SIDE_BOTH },       // Eastern Plaguelands
        { 618, 1, 55, 60, SIDE_BOTH },       // Winterspring
        { 1377, 1, 55, 60, SIDE_BOTH },      // Silithus
    };

    inline ZoneLevels const* Find(std::uint32_t zoneId)
    {
        for (ZoneLevels const& z : ZONES)
            if (z.zoneId == zoneId)
                return &z;
        return nullptr;
    }

    // A zone of the side's levelling route for this level: in its range, and a zone that side levels in.
    inline bool OnRoute(std::uint32_t zoneId, std::uint8_t level, bool horde)
    {
        ZoneLevels const* z = Find(zoneId);
        return z && level >= z->minLevel && level <= z->maxLevel && (z->sides & (horde ? SIDE_HORDE : SIDE_ALLIANCE));
    }

    // Far above the zone: more than `margin` levels over its top (a level 60 in Elwynn). False for zones without a
    // range (capitals, custom zones): a capital is a place to pass through.
    inline bool FarAbove(std::uint32_t zoneId, std::uint8_t level, std::uint8_t margin)
    {
        ZoneLevels const* z = Find(zoneId);
        return z && level > z->maxLevel + margin;
    }

    // A bot of this level fits the zone for the density: within `margin` of the range. Zones without a range:
    // the player's level decides (within `margin` of it).
    inline bool Fits(std::uint32_t zoneId, std::uint8_t level, std::uint8_t playerLevel, std::uint8_t margin)
    {
        if (ZoneLevels const* z = Find(zoneId))
            return level + margin >= z->minLevel && level <= z->maxLevel + margin;
        return level + margin >= playerLevel && level <= playerLevel + margin;
    }
}

#endif
