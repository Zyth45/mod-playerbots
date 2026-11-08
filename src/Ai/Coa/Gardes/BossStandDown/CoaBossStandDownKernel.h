/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_BOSS_STAND_DOWN_KERNEL_H
#define COA_BOSS_STAND_DOWN_KERNEL_H

#include <cstdint>
#include <string_view>

// Raid boss STAND-DOWN, the pure half (moved from Dungeon Clear's DcBossStandDown, logic unchanged). While a raid
// encounter is live, the fight belongs to the playerbots raid strategies (ACTION_RAID 60+): every Dungeon Clear combat
// rung and recovery ladder must go inert, except the encounter drivers Dungeon Clear owns inside the fight.
//
// Hysteresis is asymmetric by design: ENTER instantly (the first tick of a pull must already be gated), EXIT only after
// the signals have read clear for kExitGraceMs (submerge phases, RP pauses and the Razorgore phase flip all blink the
// signals for a moment). Header-only, no game state, so the standalone tests pin it.
namespace CoaBossStandDown
{
    constexpr std::uint32_t kExitGraceMs = 3000;

    // Per-tick memo window: dedupes strictly WITHIN one AI tick (>=100ms apart), never across ticks.
    constexpr std::uint32_t kEvalWindowMs = 50;

    struct Verdict
    {
        bool active;
        std::uint32_t lastSignalMs;  // 0 = never signalled
    };

    // Fold one observation into the hysteresis state. Unsigned subtraction keeps getMSTimeDiff's wraparound.
    inline Verdict Update(bool wasActive, std::uint32_t lastSignalMs, bool signal, std::uint32_t nowMs,
                          std::uint32_t exitGraceMs = kExitGraceMs)
    {
        if (signal)
            return {true, nowMs};
        if (!wasActive)
            return {false, lastSignalMs};
        if (nowMs - lastSignalMs >= exitGraceMs)
            return {false, lastSignalMs};
        return {true, lastSignalMs};
    }

    // What the stand-down does with ONE combat-engine action, by NAME alone (the exemption list, unchanged).
    enum class ActionVerdict : std::uint8_t
    {
        Stock,  // hand this action back to stock strength (1.0)
        Inert,  // a Dungeon Clear rung the encounter owns instead (0.0)
        Defer,  // the name says nothing: the caller's own gate decides
    };

    // `isDcAction`: the name starts with "dungeon clear".
    inline ActionVerdict ClassifyAction(std::string_view name, bool isDcAction)
    {
        // The combat event driver (BWL's Razorgore orb), the orb runner and its camp, hold-fire (its window IS a raid
        // encounter), the Oculus riders, the Karazhan chess seat, and the out-of-LOS assist (exempt as a pair with
        // `drop target`, which is deferred below).
        if (name == "dungeon clear run event combat" || name == "dungeon clear razorgore orb" ||
            name == "dungeon clear razorgore camp" || name == "dungeon clear hold fire" ||
            name == "dungeon clear oc rider" || name == "dungeon clear kz chess" ||
            name == "dungeon clear assist camp combat")
            return ActionVerdict::Stock;

        if (isDcAction)
            return ActionVerdict::Inert;

        return ActionVerdict::Defer;
    }

    // THE FIVE-MAN TABLE: a 5-man boss with nothing on the ground to drive (The Oculus's Eregos, fought from the
    // drakes). The script-state signal is unusable there, so each row names the instance guid slot of the boss.
    struct TableRow
    {
        std::uint32_t mapId;
        std::uint32_t bossEntry;
        std::uint32_t guidSlot;
    };

    inline constexpr TableRow kTable[] = {
        {578, 27656, 3},  // The Oculus, Ley-Guardian Eregos (DATA_EREGOS)
    };

    inline TableRow const* FindTableRow(std::uint32_t mapId)
    {
        for (TableRow const& r : kTable)
            if (r.mapId == mapId)
                return &r;
        return nullptr;
    }

    // ---- the per-group state (the module's home for what lived in the Dungeon Clear leader's DcRunState) ----
    struct GroupState
    {
        std::uint32_t mapId{0};
        std::uint32_t instanceId{0};
        bool active{false};             // the current verdict
        std::uint32_t signalMs{0};      // when an encounter signal last read true
        std::uint32_t evalMs{0};        // memo stamp of the last evaluation
    };

    // The group is on (mapId, instanceId) now: a group that left the map starts clean.
    inline void Follow(GroupState& s, std::uint32_t mapId, std::uint32_t instanceId)
    {
        if (s.mapId != mapId || s.instanceId != instanceId)
            s = GroupState{mapId, instanceId};
    }

    // Is the memo of this tick still fresh? (Then `s.active` is the answer and no signal is computed.)
    inline bool MemoFresh(GroupState const& s, std::uint32_t nowMs)
    {
        return s.evalMs && nowMs - s.evalMs < kEvalWindowMs;
    }

    // One evaluation, exactly as Dungeon Clear's IsActive did it on the leader's state: memo, then fold the signal.
    template <class SignalFn>
    inline bool Evaluate(GroupState& s, std::uint32_t nowMs, SignalFn&& signal)
    {
        if (MemoFresh(s, nowMs))
            return s.active;
        s.evalMs = nowMs;
        Verdict const v = Update(s.active, s.signalMs, signal(), nowMs);
        s.active = v.active;
        s.signalMs = v.lastSignalMs;
        return v.active;
    }
}

#endif
