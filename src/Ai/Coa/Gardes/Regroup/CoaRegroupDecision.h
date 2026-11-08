/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_REGROUP_DECISION_H
#define COA_REGROUP_DECISION_H

#include <cstdint>

// The contribution-gated combat regroup, from Dungeon Clear (DcRegroupDecision + the flap control of
// DungeonClearRegroupCombatTrigger), pure so tests/coa_gardes_regroup.cpp reads it as is. A follower in combat only
// walks back to the fight when it has NO useful work it can do from where it stands; the hard outer tether is the
// drifted-into-nowhere safety net. The verdict is Dungeon Clear's, unchanged.
namespace CoaRegroupDecision
{
    // None: the bot can contribute (or must not move). Reconnect: it cannot from here, walk to a role-correct standoff
    // with LOS on the fight (debounced). HardTether: past the outer tether, reconnect at once.
    enum class RegroupVerdict
    {
        None,
        Reconnect,
        HardTether
    };

    struct RegroupInputs
    {
        bool isHealer = false;           // PlayerbotAI::IsHeal(bot)
        bool isMelee = false;            // PlayerbotAI::IsMelee(bot): the action's radius only
        bool casting = false;            // a generic or channeled spell is in flight
        bool ccd = false;                // stunned / fleeing / confused / rooted
        bool hasVisibleAttacker = false; // stock LOS-filtered `attackers` non-empty
        bool hasLosTarget = false;       // live, valid, in-LOS `current target`
        bool hasHurtHealTarget = false;  // a member under the heal floor (healers)
        bool tankLos = false;            // bot->IsWithinLOSInMap(tank)
        float tankDist2d = 0.0f;         // bot->GetExactDist2d(tank)
        float healRange = 0.0f;          // botAI->GetRange("heal")
        float hardTether = 0.0f;         // CombatRegroupDistance
        float slack = 0.0f;              // CombatRegroupSlack
    };

    // In priority order:
    //   1. casting || ccd -> None (never clip a cast, never fight CC), even past the tether;
    //   2. tankDist2d > hardTether -> HardTether;
    //   3. DPS: a visible attacker or an in-LOS target -> None, else Reconnect (the attacker list is threat-derived and
    //      reads empty before anyone landed threat, so an in-LOS target counts as work too, or the rung mutes the DPS);
    //   4. healer: a hurt heal target -> None (the heal reposition owns it); else pre-position when out of LOS of the
    //      tank or beyond heal range less the slack.
    inline RegroupVerdict DecideCombatRegroup(RegroupInputs const& in)
    {
        if (in.casting || in.ccd)
            return RegroupVerdict::None;
        if (in.tankDist2d > in.hardTether)
            return RegroupVerdict::HardTether;
        if (!in.isHealer)
            return (in.hasVisibleAttacker || in.hasLosTarget) ? RegroupVerdict::None : RegroupVerdict::Reconnect;
        if (in.hasHurtHealTarget)
            return RegroupVerdict::None;
        if (!in.tankLos || in.tankDist2d > (in.healRange - in.slack))
            return RegroupVerdict::Reconnect;
        return RegroupVerdict::None;
    }

    // Whether the rung may run at all for this bot this tick (before the verdict). `hasTank`: the group's reference
    // tank (CoaGroupTank: alive, on the bot's map). The reference tank itself never regroups (it drives); a bot tank
    // leaves a HUMAN reference tank alone (his off-tank); a passive advanced-pull camp hold of a Dungeon Clear run owns
    // the positioning.
    struct GateInputs
    {
        bool botAlive = true;
        bool inCombat = false;
        bool featureOn = true;
        bool hasTank = false;
        bool tankIsSelf = false;
        bool tankSameMap = true;
        bool tankIsHuman = false;
        bool botIsTank = false;
        bool campHoldPassive = false;
    };

    constexpr bool GateOpen(GateInputs const& g)
    {
        return g.botAlive && g.inCombat && g.featureOn && g.hasTank && !g.tankIsSelf && g.tankSameMap &&
               !(g.tankIsHuman && g.botIsTank) && !g.campHoldPassive;
    }

    // The flap control: debounce in (the Reconnect verdict must hold DEBOUNCE_MS), a latch (keep firing until the
    // verdict clears), a cooldown after a reconnect, and the hard tether bypassing all three. Times are getMSTime.
    inline constexpr std::uint32_t DEBOUNCE_MS = 2000;

    struct RegroupLatch
    {
        std::uint32_t pendingSince = 0;  // first time the Reconnect verdict held (0 = not pending)
        std::uint32_t cooldownUntil = 0; // no non-emergency fire before this (0 = none)
        bool latched = false;            // firing: keep firing until the verdict clears
    };

    // The gate closed: a latch never survives into the next fight.
    inline void StandDown(RegroupLatch& s)
    {
        s.latched = false;
        s.pendingSince = 0;
    }

    // Returns whether the rung fires this tick.
    inline bool Step(RegroupLatch& s, RegroupVerdict verdict, std::uint32_t now, std::uint32_t cooldownMs)
    {
        bool const wasLatched = s.latched;
        if (verdict == RegroupVerdict::HardTether)
        {
            s.latched = true;
            s.pendingSince = 0;
            return true;
        }
        if (verdict == RegroupVerdict::None)
        {
            if (wasLatched)
            {
                s.cooldownUntil = now + cooldownMs;
                if (s.cooldownUntil == 0)
                    s.cooldownUntil = 1;
            }
            s.latched = false;
            s.pendingSince = 0;
            return false;
        }
        if (s.latched)
            return true;
        if (s.cooldownUntil != 0 && std::int32_t(s.cooldownUntil - now) > 0)
        {
            s.pendingSince = 0;
            return false;
        }
        if (s.pendingSince == 0)
            s.pendingSince = now ? now : 1;
        if (std::uint32_t(now - s.pendingSince) >= DEBOUNCE_MS)
        {
            s.latched = true;
            return true;
        }
        return false;
    }
}

#endif
