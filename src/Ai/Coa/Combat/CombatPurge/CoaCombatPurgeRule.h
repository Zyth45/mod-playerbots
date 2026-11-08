/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_COMBAT_PURGE_RULE_H
#define COA_COMBAT_PURGE_RULE_H

#include <cstdint>

// The pure half of the combat purge (CoA Combat B4, from Dungeon Clear's DcCombatPurge and its registry): which
// creature may be dropped, and the clock. No game type, so tests/coa_combat_combat_purge.cpp checks it on its own.
//
// The table is deliberately not a reachability test: a runtime navmesh answer called a holder 350 yd away through rock
// "reachable". Each row is a hand-vetted claim about one creature on one map, checked against the world data.
namespace CoaCombatPurgeRule
{
    // When a listed creature may be dropped.
    //   Always:  it holds the party by construction; fighting it is never the encounter.
    //   Evading: a real opponent that only becomes unendable once it has given up (kept for future rows).
    enum class When : std::uint8_t
    {
        Always,
        Evading
    };

    struct Row
    {
        std::uint32_t mapId;
        std::uint32_t entry;  // the normal entry; heroic spawns keep it
        When when;
    };

    // Shadowfang Keep (33), [DND] Valentine Boss - Vial Bunny (36530): holiday triggers of the Apothecary Hummel event
    // in the courtyard, hostile and immune, never die and never evade; one that holds a member keeps the party flagged
    // for good (no rest, no out-of-combat resurrection).
    //
    // Dire Maul West (429), Dire Maul Crystal Totem (13916): set dressing by the west Warpwood hall, unit_flags 770
    // (NON_ATTACKABLE | IMMUNE_TO_PC | IMMUNE_TO_NPC), no script: it never dies and never evades, and held three
    // members at 14-24 yd for ten minutes.
    inline constexpr Row ROWS[] = {
        {33, 36530, When::Always},
        {429, 13916, When::Always},
    };

    // How long a member must be held by a listed creature before the purge (seconds; Dungeon Clear's
    // UnreachableCombatPurgeSecs). The target bar lasts as long. 0 switches the purge off.
    inline constexpr std::uint32_t DEFAULT_WINDOW_SECONDS = 60;
    inline constexpr std::uint32_t MAX_WINDOW_SECONDS = 3600;

    // May combat with `entry` on `mapId` be dropped? `isEvading` is the live creature's evade state (ignored by Always
    // rows, the whole test for Evading ones).
    inline bool IsPurgeable(std::uint32_t mapId, std::uint32_t entry, bool isEvading)
    {
        for (Row const& r : ROWS)
            if (r.mapId == mapId && r.entry == entry)
                return r.when == When::Always || isEvading;
        return false;
    }

    // The cheap gate before any combat reference is walked.
    inline bool HasRowsFor(std::uint32_t mapId)
    {
        for (Row const& r : ROWS)
            if (r.mapId == mapId)
                return true;
        return false;
    }

    // The member's clock: `heldMs` grows while a listed creature holds the member and falls back to 0 the moment none
    // does. True once it reaches `windowMs`, which restarts it (the next purge waits a full window). `windowMs` 0: off.
    inline bool Step(std::uint32_t& heldMs, std::uint32_t diffMs, bool held, std::uint32_t windowMs)
    {
        if (!held || windowMs == 0)
        {
            heldMs = 0;
            return false;
        }
        heldMs += diffMs;
        if (heldMs < windowMs)
            return false;
        heldMs = 0;
        return true;
    }

    // The window in milliseconds: Dungeon Clear's old key when the player has it (negative = absent), else the
    // default; clamped to MAX_WINDOW_SECONDS.
    inline std::uint32_t WindowMs(std::int64_t legacySeconds)
    {
        std::int64_t s = legacySeconds >= 0 ? legacySeconds : static_cast<std::int64_t>(DEFAULT_WINDOW_SECONDS);
        if (s > static_cast<std::int64_t>(MAX_WINDOW_SECONDS))
            s = MAX_WINDOW_SECONDS;
        return static_cast<std::uint32_t>(s) * 1000u;
    }
}

#endif
