/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Dungeon Clear 2.9.4 (coa/2.9.4 @ 0590afb), Data/Events/DungeonEventTables.h, DcBlackrockDepths (Lyceum box, Iron Hall) - copied verbatim as the non-regression ORACLE of the
// module's party combat guards. Only the includes are adapted so it compiles alone (no core header). Never edit.
#ifndef DC294_BRD_TABLES_H
#define DC294_BRD_TABLES_H
#include <cstdint>
using uint32 = std::uint32_t;
namespace DcBlackrockDepths {
    constexpr uint32 MAP_ID = 230;
    constexpr float LYCEUM_BOX_X_MIN = 1275.0f;
    constexpr float LYCEUM_BOX_X_MAX = 1480.0f;
    constexpr float LYCEUM_BOX_Y_MIN = -560.0f;
    constexpr float LYCEUM_BOX_Y_MAX = -305.0f;
    constexpr float LYCEUM_BOX_Z_MIN = -102.0f;
    constexpr float LYCEUM_BOX_Z_MAX = -80.0f;
    constexpr uint32 NPC_MAGMUS = 9938;
    constexpr uint32 NPC_IRONHAND_GUARDIAN = 8982;      // stock AC
    constexpr uint32 NPC_IRONHAND_GUARDIAN_IP = 108982; // mod-individual-progression
    constexpr uint32 GO_THRONE_DOOR = 170575;
    constexpr float IRONHAND_FIRE_RADIUS = 30.0f;       // spell 15538's radius
    struct HallPoint { float x, y; };
    constexpr HallPoint IRONHAND_GUARDIANS[] = {
        { 1407.29f, -587.30f }, { 1353.43f, -587.32f },
        { 1406.88f, -632.44f }, { 1353.92f, -632.75f },
        { 1407.67f, -677.72f }, { 1353.16f, -677.38f },
    };
    constexpr float MAGMUS_X = 1380.72f;
    constexpr float MAGMUS_Y = -659.31f;
    constexpr float MAGMUS_Z = -91.97f;
    // The fire-free gap Magmus spawns in, measured against the six circles: a
    // diamond ~14yd across (x) by ~17yd long (y) centred on (1380.5, -655.5).
    // These are its four tips pulled in by a yard, i.e. points the party can
    // stand on with nothing landing on them. (The plan's band rectangle, x
    // 1372-1388 by y -648..-663, does NOT fit: its corners are 23-24yd from the
    // nearest guardians.)
    constexpr HallPoint IRON_HALL_SAFE[] = {
        { 1380.5f, -647.8f }, { 1380.5f, -663.6f },   // north / south tips
        { 1374.7f, -655.5f }, { 1386.5f, -655.5f },   // west / east tips
    };
    // Fight-in-place zone: the hall between the Golem doors (y -555.9) and the
    // throne door (y -710.5), wall to wall.
    constexpr float IRON_HALL_MIN_X = 1340.0f;
    constexpr float IRON_HALL_MAX_X = 1420.0f;
    constexpr float IRON_HALL_MIN_Y = -705.0f;
    constexpr float IRON_HALL_MAX_Y = -562.0f;

    // The safe-spot hop (DcIronHall, DungeonClearIronHallSafeSpot). A member may
    // walk through the fire but never STOP in it, so the hall is a chain of four
    // stopping places on the centre line, each a fire-free gap between the
    // statue bands: north by the Golem doors, gap 1, gap 2 (Magmus), south by
    // the throne door.
    //
    // "In the fire" is 32yd, not the raw 30: AC's area search measures to the
    // target's edge (IsWithinDist adds the unit's ~1.5yd reach), and half a yard
    // more covers drift. Every slot below sits at >= 33yd from every statue, so a
    // bot that arrives is a yard clear of the test that sent it — it cannot
    // re-fire on landing.
    constexpr float IRON_HALL_FIRE_KEEPOUT = 32.0f;
    // The hop's box: the fight-in-place box widened to the doors on both ends,
    // since the north and south stops stand past its y edges.
    constexpr float IRON_HALL_HOP_MIN_Y = -712.0f;
    constexpr float IRON_HALL_HOP_MAX_Y = -554.0f;
    constexpr float IRON_HALL_HOP_MIN_Z = -100.0f;
    constexpr float IRON_HALL_HOP_MAX_Z = -84.0f;
    constexpr float IRON_HALL_FLOOR_Z = -91.97f;
    constexpr uint32 IRON_HALL_ZONE_COUNT = 4;
    constexpr HallPoint IRON_HALL_ZONES[IRON_HALL_ZONE_COUNT] = {
        { 1380.5f, -562.5f },   // north, by the Golem doors
        { 1380.3f, -609.7f },   // gap 1
        { 1380.6f, -655.5f },   // gap 2, Magmus's
        { 1380.5f, -702.0f },   // south, by the throne door
    };
    // Five slots per stop, so a party does not stack on one point. The gaps are
    // diamonds ~8yd across by ~10 long at 32yd, so the slots are a 2.5yd cross.
    constexpr uint32 IRON_HALL_SLOT_COUNT = 5;
    constexpr HallPoint IRON_HALL_SLOT_OFFSETS[IRON_HALL_SLOT_COUNT] = {
        { 0.0f, 0.0f }, { 2.5f, 0.0f }, { -2.5f, 0.0f }, { 0.0f, 2.5f }, { 0.0f, -2.5f },
    };
}
#endif
