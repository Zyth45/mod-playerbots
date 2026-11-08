/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_STUCK_COMBAT_RULE_H
#define COA_STUCK_COMBAT_RULE_H

#include <cstdint>

// The pure half of the stuck combat breaker (CoA Combat B3, from Dungeon Clear's phantom-combat hatch): no game type,
// so tests/coa_combat_stuck_combat.cpp checks it on its own. The game reads live in CoaStuckCombat.cpp.
namespace CoaStuckCombatRule
{
    // A holder this close is a fight whatever the numbers say (yards; Dungeon Clear's DC_ENGAGE_RANGE).
    inline constexpr float ENGAGE_RANGE = 22.0f;
    // Past this, a combat reference has outlived the geometry it was made in: never a legitimate holder, whatever the
    // navmesh answers (yards; DC_ENGAGEMENT_RADIUS).
    inline constexpr float ENGAGEMENT_RADIUS = 100.0f;
    // A holder must improve its closest-ever distance by this much to count as closing (yards; DC_STUCK_DISPLACEMENT).
    inline constexpr float CLOSING_STEP = 0.5f;
    // Relevance in both engines: above every fight action of the ladder it guards (DcRelevance::BreakStuckCombat).
    inline constexpr float RELEVANCE = 65.0f;
    // Default and bounds of the timeout (seconds; Dungeon Clear's StuckCombatTimeout). 0 switches the breaker off.
    inline constexpr float DEFAULT_TIMEOUT_SECONDS = 15.0f;
    inline constexpr float MAX_TIMEOUT_SECONDS = 120.0f;

    // Flagged in combat with nothing to fight: nothing hitting us, no victim, no legitimate holder.
    inline bool IsPhantomCombat(bool inCombat, bool hasAttacker, bool hasVictim, bool hasLegitimateHolder)
    {
        return inCombat && !hasAttacker && !hasVictim && !hasLegitimateHolder;
    }

    // A legitimate holder (alive, not evading, reachable, allowed by its script to attack us) is prosecuting the fight
    // when the nearest one we can SEE is inside engage range, or when the nearest one is closing on us. In an instance
    // a creature never leashes, so a mob that tagged us and stopped would hold the flag forever while reading as legit.
    inline bool IsHolderProsecutingFight(bool haveHolder, float seenHolderDist, float engageRange, bool closing)
    {
        return haveHolder && (seenHolderDist <= engageRange || closing);
    }

    // A creature with the TRIGGER extra flag is an invisible script helper: no bot can ever target it, so its combat
    // reference has no end state. Deliberately not "not selectable": real bosses wear that flag for part of a fight.
    inline bool IsUnresolvableCombatHolder(bool isCreature, bool isTrigger)
    {
        return isCreature && isTrigger;
    }

    // The streak clock. `sinceMs` is the caller's latch (0 = not streaking): armed on the first phantom tick, cleared
    // the instant the phantom state breaks, so a one-tick gap never trips it. True once the phantom state has held for
    // `timeoutMs` without a break. `timeoutMs` 0 switches it off.
    inline bool ShouldBreakStuckCombat(bool phantom, std::uint32_t now, std::uint32_t timeoutMs, std::uint32_t& sinceMs)
    {
        if (!phantom || timeoutMs == 0)
        {
            sinceMs = 0;
            return false;
        }
        if (sinceMs == 0)
            sinceMs = now ? now : 1;  // 0 means "not armed"
        // False on the arming tick at ms 0 and on a backward clock step: no time has elapsed yet.
        return now >= sinceMs && (now - sinceMs) >= timeoutMs;
    }

    // Closing-distance watch over the nearest legitimate holder. The first sample arms it and counts as closing; after
    // that, closing means beating the closest distance seen so far by at least `minClose`.
    struct ClosingWatch
    {
        float bestDist = -1.0f;  // < 0: not armed

        void Reset() { bestDist = -1.0f; }

        bool Tick(float dist, float minClose)
        {
            if (bestDist < 0.0f || dist < bestDist - minClose)
            {
                bestDist = dist;
                return true;
            }
            return false;
        }
    };

    // The timeout in milliseconds, from the settings: Dungeon Clear's old key when the player has it (a negative
    // `legacySeconds` means absent), else the module's key; clamped to [0, MAX_TIMEOUT_SECONDS].
    inline std::uint32_t TimeoutMs(float legacySeconds, float moduleSeconds)
    {
        float s = legacySeconds >= 0.0f ? legacySeconds : moduleSeconds;
        if (!(s > 0.0f))  // also NaN
            return 0;
        if (s > MAX_TIMEOUT_SECONDS)
            s = MAX_TIMEOUT_SECONDS;
        return static_cast<std::uint32_t>(s * 1000.0f);
    }
}

#endif
