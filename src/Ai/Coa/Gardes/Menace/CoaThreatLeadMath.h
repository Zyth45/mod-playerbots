/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_THREAT_LEAD_MATH_H
#define COA_THREAT_LEAD_MATH_H

#include <cstdint>

// The pure rules of the threat lead and of the assist on the reference tank's fight (combat guards that came from
// Dungeon Clear, 07/11: DungeonClearMath::ShouldHoldThreatLead / ShouldReleaseFollower, DcPlayerLedDecision). No
// game object here, so that tests/coa_gardes_menace_math.cpp can check every branch. Times are getMSTime() stamps;
// 0 always means "none".
namespace CoaThreatLeadMath
{
    // Hold applied when the tank has not landed a hit yet: the damage dealers wait at most lead + this from the tank's
    // combat flag, so a tank that opens with a taunt, or is stunned on the opener, cannot freeze the group.
    constexpr std::uint32_t NoHitCapMs = 2500;

    // A damage dealer must not deal damage yet: the tank's fight is live (`combatSinceMs`) and less than `leadMs` has
    // passed since the tank's first hit (`firstHitMs`), or, without a hit, since its combat flag plus `noHitCapMs`.
    // Never holds an exempt member (a tank), a member something attacks (self-defence), a tank under `panicHpPct`
    // (<= 0 switches that bypass off), or with `leadMs` == 0 (the lead switched off).
    constexpr bool ShouldHoldThreatLead(bool exempt, bool beingAttacked, std::uint32_t combatSinceMs,
                                        std::uint32_t firstHitMs, std::uint32_t now, std::uint32_t leadMs,
                                        std::uint32_t noHitCapMs, float tankHealthPct, float panicHpPct)
    {
        if (exempt || beingAttacked || leadMs == 0 || combatSinceMs == 0)
            return false;
        if (panicHpPct > 0.0f && tankHealthPct < panicHpPct)
            return false;
        // Guard the unsigned subtractions against a stamp a tick ahead of `now`.
        if (firstHitMs != 0)
            return now < firstHitMs || now - firstHitMs < leadMs;
        return now < combatSinceMs || now - combatSinceMs < leadMs + noHitCapMs;
    }

    // Whether a member may move onto the tank's fight now. Healers always (a withheld heal is a wipe); a member
    // already in combat (it is on the threat table, walking into sight cannot steal anything); the lead switched off;
    // no combat stamp (nothing to wait for); a tank under the panic health; else once `leadMs` has passed since the
    // tank's combat start.
    constexpr bool ShouldReleaseFollower(bool isHealer, bool alreadyInCombat, std::uint32_t combatSinceMs,
                                         std::uint32_t now, std::uint32_t leadMs, float tankHealthPct,
                                         float panicHpPct)
    {
        if (isHealer || alreadyInCombat || leadMs == 0 || combatSinceMs == 0)
            return true;
        if (panicHpPct > 0.0f && tankHealthPct < panicHpPct)
            return true;
        std::uint32_t const elapsed = now > combatSinceMs ? now - combatSinceMs : 0u;
        return elapsed >= leadMs;
    }

    // The tank's fight clock (one per reference tank). Entering combat starts a new fight; the first damage on a
    // non-player after that is the first hit. A hit whose enter-combat edge was missed starts the fight itself.
    struct FightClock
    {
        std::uint32_t combatSinceMs = 0;
        std::uint32_t firstHitMs = 0;
        // Last time the group was seen fighting (assist latch), 0 = not latched.
        std::uint32_t engagedLatchMs = 0;

        static constexpr std::uint32_t NonZero(std::uint32_t now) { return now != 0 ? now : 1u; }

        constexpr void EnterCombat(std::uint32_t now)
        {
            combatSinceMs = NonZero(now);
            firstHitMs = 0;
        }

        constexpr void Hit(std::uint32_t now)
        {
            if (combatSinceMs == 0)
                combatSinceMs = NonZero(now);
            if (firstHitMs == 0 || firstHitMs < combatSinceMs)
                firstHitMs = NonZero(now);
        }

        // The fight is over (the tank left combat): nothing to hold on.
        constexpr void LeaveCombat()
        {
            combatSinceMs = 0;
            firstHitMs = 0;
        }

        // The first hit of the current fight: a stamp older than the fight is stale.
        constexpr std::uint32_t CurrentFirstHit() const
        {
            return (firstHitMs != 0 && firstHitMs < combatSinceMs) ? 0u : firstHitMs;
        }

        constexpr bool Empty() const { return combatSinceMs == 0 && firstHitMs == 0 && engagedLatchMs == 0; }
    };

    // Hysteresis over "is the group fighting?": a positive read arms it, and it stays engaged `graceMs` after the last
    // positive read, so one stale read or a one-tick combat gap does not drop the group out of the fight. 0 = no latch.
    constexpr bool EngagedLatched(bool live, std::uint32_t now, std::uint32_t graceMs, std::uint32_t& latchMs)
    {
        if (graceMs == 0)
            return live;
        if (live)
        {
            latchMs = FightClock::NonZero(now);
            return true;
        }
        if (latchMs == 0)
            return false;
        if (now - latchMs < graceMs)
            return true;
        latchMs = 0;
        return false;
    }

    // A bot tank of the group is the player's off-tank while a human tanks: the rungs that move a bot onto the tank's
    // fight stand down for it (its targets are the player's orders).
    constexpr bool MemberAssists(bool tankIsHuman, bool botIsTank) { return !(tankIsHuman && botIsTank); }

    // Whether a member can hit `dist` away from where it stands: melee inside `meleeRange`, ranged inside spell range
    // plus both combat reaches (the stock reach-spell stop point, so the guard and stock never leave a dead band).
    constexpr bool IsWithinAssistAttackRange(bool isMelee, float dist, float meleeRange, float spellRange,
                                             float combatReachSum)
    {
        return isMelee ? dist <= meleeRange : dist <= spellRange + combatReachSum;
    }

    // The close-in on a mob out of sight was refused (no move) on the same target for `limitMs`: give it up.
    // `refusedSince` is the caller's clock (0 = not refusing).
    constexpr bool AssistCloseGivenUp(bool moved, bool sameTarget, std::uint32_t now, std::uint32_t limitMs,
                                      std::uint32_t& refusedSince)
    {
        if (moved)
        {
            refusedSince = 0;
            return false;
        }
        if (!sameTarget || refusedSince == 0)
        {
            refusedSince = FightClock::NonZero(now);
            return false;
        }
        return now - refusedSince >= limitMs;
    }

    // A mob within this height of the bot stands on its level (no path probe); past it, a complete route that ends on
    // the mob's level and is not a long detour is required (Dungeon Clear's IsLevelReachable: a Blackwing Warlock
    // 24.5 yd straight up drew a raid through a floor).
    constexpr float ZLevelTolerance = 5.0f;
    constexpr float DetourRatio = 2.0f;
    constexpr float DetourSlack = 20.0f;
    constexpr bool WithinDetour(float straight, float pathLength)
    {
        float const byRatio = straight * DetourRatio;
        float const bySlack = straight + DetourSlack;
        return pathLength <= (byRatio > bySlack ? byRatio : bySlack);
    }

    // The settings layer for a map: a raid reads the raid value when it has one, a heroic-tier map the heroic value
    // when it has one, else the normal value.
    constexpr float Tiered(float normal, float heroic, float raid, bool hasHeroic, bool hasRaid, bool heroicTier,
                           bool isRaid)
    {
        if (isRaid && hasRaid)
            return raid;
        if (heroicTier && hasHeroic)
            return heroic;
        return normal;
    }
}

#endif
