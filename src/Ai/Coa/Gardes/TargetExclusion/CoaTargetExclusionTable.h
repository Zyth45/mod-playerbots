/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_TARGET_EXCLUSION_TABLE_H
#define COA_TARGET_EXCLUSION_TABLE_H

#include <cstdint>

// The pure half of the target exclusion guard (hold fire), moved from Dungeon Clear (DcTargetExclusionRegistry):
// creatures the DPS must not SHOOT, for a window the world itself defines. Header-only, no game state, so the
// standalone tests can pin the table and the decision. The live gates are in CoaTargetExclusion.cpp.
//
// The bar is "killing this, in this window, LOSES THE RUN", not "wastes damage". Razorgore is the case it was written
// for: in phase 1, killing him before the last egg breaks casts 20038 and instakills the raid; in phase 2 he is the
// objective. So every row is WINDOWED by a live gate, and the gate is read per target pick, never latched.
//
// `alsoTank`: does the bar reach the TANK's pick too? False for Razorgore (an off-tank must still hold him between
// mind controls); true for an out-of-order boss (a tank answering a boss two rooms ahead walks the raid there) and for
// creatures nobody may hold at all.
namespace CoaTargetExclusion
{
    // Entries and instance data slots, from Dungeon Clear's tables (DungeonEventTables.h, DcChessBoard.h).
    namespace Bwl
    {
        inline constexpr std::uint32_t MAP_ID = 469;
        inline constexpr std::uint32_t NPC_RAZORGORE = 12435;
        inline constexpr std::uint32_t GO_BLACK_DRAGON_EGG = 177807;
        inline constexpr std::uint32_t DATA_EGG_EVENT = 2;
        inline constexpr float ROOM_SCAN = 150.0f;
        inline constexpr std::uint32_t NPC_BROODLORD_LASHLAYER = 12017;
        inline constexpr std::uint32_t BROODLORD_ENCOUNTER_INDEX = 2;
        inline constexpr std::uint32_t NPC_FIREMAW = 11983;
        inline constexpr std::uint32_t NPC_EBONROC = 14601;
        inline constexpr std::uint32_t NPC_FLAMEGOR = 11981;
        inline constexpr std::uint32_t NPC_CHROMAGGUS = 14020;
    }

    namespace Gundrak
    {
        inline constexpr std::uint32_t MAP_ID = 604;
        inline constexpr std::uint32_t NPC_DRAKKARI_RAIDER = 29982;
    }

    namespace HallsOfReflection
    {
        inline constexpr std::uint32_t MAP_ID = 668;
        inline constexpr std::uint32_t NPC_LICH_KING = 36954;
        inline constexpr std::uint32_t DATA_LICH_KING = 2;
    }

    namespace Karazhan
    {
        inline constexpr std::uint32_t MAP_ID = 532;
        inline constexpr std::uint32_t NPC_PAWN_A = 17211, NPC_PAWN_H = 17469;
        inline constexpr std::uint32_t NPC_ROOK_A = 21160, NPC_ROOK_H = 21726;
        inline constexpr std::uint32_t NPC_KNIGHT_A = 21664, NPC_KNIGHT_H = 21748;
        inline constexpr std::uint32_t NPC_BISHOP_A = 21682, NPC_BISHOP_H = 21747;
        inline constexpr std::uint32_t NPC_QUEEN_A = 21683, NPC_QUEEN_H = 21750;
        inline constexpr std::uint32_t NPC_KING_A = 21684, NPC_KING_H = 21752;
        inline constexpr std::uint32_t NPC_MOVE_TRIGGER = 22519;
        inline constexpr std::uint32_t NPC_FIRE = 22521;
    }

    namespace TrialOfTheChampion
    {
        inline constexpr std::uint32_t MAP_ID = 650;
        inline constexpr std::uint32_t DATA_ARGENT_CHAMPION = 15;
        inline constexpr std::uint32_t NPC_PALETRESS = 34928;
        inline constexpr std::uint32_t SPELL_REFLECTIVE_SHIELD = 66515;
    }

    // Which live gate keeps a row in force. Always = no gate (the chess pieces: never progress to attack).
    enum class Gate : std::uint8_t
    {
        Always,
        RazorgoreEggPhase,        // an egg still stands (the instance's egg event is not DONE)
        BwlDrakeHallOutOfOrder,   // Broodlord still stands and was not skipped by the operator
        GundrakRaiderJustPurged,  // a combat purge just dropped this raider (an active Dungeon Clear run only)
        HorEscapeRunning,         // the Lich King escape is IN_PROGRESS
        PaletressShielded,        // Paletress carries Reflective Shield (her Memory lives)
    };

    struct Row
    {
        std::uint32_t mapId;
        std::uint32_t entry;
        Gate gate;
        bool alsoTank;
    };

    // Dungeon Clear 2.9.4's rows, same order, same windows, same tank reach.
    inline constexpr Row kRows[] = {
        {Bwl::MAP_ID, Bwl::NPC_RAZORGORE, Gate::RazorgoreEggPhase, false},
        {Bwl::MAP_ID, Bwl::NPC_FIREMAW, Gate::BwlDrakeHallOutOfOrder, true},
        {Bwl::MAP_ID, Bwl::NPC_EBONROC, Gate::BwlDrakeHallOutOfOrder, true},
        {Bwl::MAP_ID, Bwl::NPC_FLAMEGOR, Gate::BwlDrakeHallOutOfOrder, true},
        {Bwl::MAP_ID, Bwl::NPC_CHROMAGGUS, Gate::BwlDrakeHallOutOfOrder, true},
        {Gundrak::MAP_ID, Gundrak::NPC_DRAKKARI_RAIDER, Gate::GundrakRaiderJustPurged, true},
        {HallsOfReflection::MAP_ID, HallsOfReflection::NPC_LICH_KING, Gate::HorEscapeRunning, true},
        {Karazhan::MAP_ID, Karazhan::NPC_PAWN_A, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_PAWN_H, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_ROOK_A, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_ROOK_H, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_KNIGHT_A, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_KNIGHT_H, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_BISHOP_A, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_BISHOP_H, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_QUEEN_A, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_QUEEN_H, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_KING_A, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_KING_H, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_MOVE_TRIGGER, Gate::Always, true},
        {Karazhan::MAP_ID, Karazhan::NPC_FIRE, Gate::Always, true},
        {TrialOfTheChampion::MAP_ID, TrialOfTheChampion::NPC_PALETRESS, Gate::PaletressShielded, false},
    };

    // Does this map have any rows at all? The fast gate behind Strategy::HasTargetExclusions.
    inline bool HasRowsFor(std::uint32_t mapId)
    {
        for (Row const& r : kRows)
            if (r.mapId == mapId)
                return true;
        return false;
    }

    // Is `entry` excluded right now? The FIRST row matching (map, entry) decides: a tank pick is only barred by an
    // `alsoTank` row, and the row's live gate (`inForce(gate)`) must hold.
    template <class InForce>
    inline bool Decide(std::uint32_t mapId, std::uint32_t entry, bool forTank, InForce&& inForce)
    {
        for (Row const& r : kRows)
            if (r.mapId == mapId && r.entry == entry)
            {
                if (forTank && !r.alsoTank)
                    return false;
                return r.gate == Gate::Always || inForce(r.gate);
            }
        return false;
    }

    // ---- the pure parts of the live gates ----

    // Razorgore: with an instance script, the egg event decides (a range-bounded scan reads phase 2 the moment the
    // raid drifts away from the last eggs, and wipes on 20038); the egg scan is only the no-script fallback.
    inline bool RazorgoreEggPhase(bool hasScript, bool eggEventDone, bool anEggStandsInScan)
    {
        return hasScript ? !eggEventDone : anEggStandsInScan;
    }

    // The drake hall: barred while Broodlord stands, unless the operator skipped him. No instance script reads as
    // "still standing", the safe direction.
    inline bool BwlDrakeHallOutOfOrder(bool broodlordSkipped, bool hasScript, bool broodlordDone)
    {
        if (broodlordSkipped)
            return false;
        return !hasScript || !broodlordDone;
    }

    // The Lich King: the whole escape, and only on Halls of Reflection.
    inline bool HorEscapeRunning(bool onHorMap, bool hasScript, bool lichKingInProgress)
    {
        return onHorMap && hasScript && lichKingInProgress;
    }

    // Paletress: the Argent champion slot holds a living Paletress carrying Reflective Shield.
    inline bool PaletressShielded(bool onTocMap, bool championAlive, bool championIsPaletress, bool shielded)
    {
        return onTocMap && championAlive && championIsPaletress && shielded;
    }
}

#endif
