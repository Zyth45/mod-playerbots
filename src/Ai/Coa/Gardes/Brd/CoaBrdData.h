/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_BRD_DATA_H
#define COA_BRD_DATA_H

#include <cstdint>

// Blackrock Depths (map 230): the numbers of the Lyceum close-up and the Iron Hall safe spot, copied from Dungeon
// Clear's DcBlackrockDepths tables (Data/Events/DungeonEventTables.h), read-only. The reasoning for each is there.
namespace CoaBrd
{
    constexpr std::uint32_t MAP_ID = 230;

    // --- the Lyceum: the never-target Reservist box plus a floor band (every spawn stands at z -96..-88).
    constexpr float LYCEUM_BOX_X_MIN = 1275.0f;
    constexpr float LYCEUM_BOX_X_MAX = 1480.0f;
    constexpr float LYCEUM_BOX_Y_MIN = -560.0f;
    constexpr float LYCEUM_BOX_Y_MAX = -305.0f;
    constexpr float LYCEUM_BOX_Z_MIN = -102.0f;
    constexpr float LYCEUM_BOX_Z_MAX = -80.0f;

    // --- the Iron Hall (Magmus): six Ironhand Guardians pulse Gout of Flame (15529 -> 15538, a 30yd circle) under
    // mod-individual-progression, in or out of combat. A member may walk through the fire, never stop in it.
    constexpr std::uint32_t NPC_MAGMUS = 9938;
    struct HallPoint
    {
        float x, y;
    };
    constexpr HallPoint IRONHAND_GUARDIANS[] = {
        {1407.29f, -587.30f}, {1353.43f, -587.32f}, {1406.88f, -632.44f},
        {1353.92f, -632.75f}, {1407.67f, -677.72f}, {1353.16f, -677.38f},
    };
    constexpr float IRON_HALL_MIN_X = 1340.0f;
    constexpr float IRON_HALL_MAX_X = 1420.0f;
    // "In the fire" is 32yd, not the raw 30 (the area search measures to the target's edge, plus drift).
    constexpr float IRON_HALL_FIRE_KEEPOUT = 32.0f;
    constexpr float IRON_HALL_HOP_MIN_Y = -712.0f;
    constexpr float IRON_HALL_HOP_MAX_Y = -554.0f;
    constexpr float IRON_HALL_HOP_MIN_Z = -100.0f;
    constexpr float IRON_HALL_HOP_MAX_Z = -84.0f;
    constexpr float IRON_HALL_FLOOR_Z = -91.97f;
    // Four stops on the centre line, north to south: by the Golem doors, gap 1, gap 2 (Magmus), by the throne door.
    constexpr std::uint32_t IRON_HALL_ZONE_COUNT = 4;
    constexpr HallPoint IRON_HALL_ZONES[IRON_HALL_ZONE_COUNT] = {
        {1380.5f, -562.5f},
        {1380.3f, -609.7f},
        {1380.6f, -655.5f},
        {1380.5f, -702.0f},
    };
    // Five slots per stop, a 2.5yd cross, so a party does not stack on one point.
    constexpr std::uint32_t IRON_HALL_SLOT_COUNT = 5;
    constexpr HallPoint IRON_HALL_SLOT_OFFSETS[IRON_HALL_SLOT_COUNT] = {
        {0.0f, 0.0f}, {2.5f, 0.0f}, {-2.5f, 0.0f}, {0.0f, 2.5f}, {0.0f, -2.5f},
    };
}

#endif
