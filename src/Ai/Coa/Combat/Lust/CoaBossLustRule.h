/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COABOSSLUSTRULE_H
#define _PLAYERBOT_COABOSSLUSTRULE_H

#include <cstdint>
#include <string>

// When a grouped bot casts its group burst (a shaman's Bloodlust or Heroism, a CoA class burst): only on a boss, at the pull or under a health threshold. Pure, so
// tests/coa_combat_boss_lust.cpp reads it as is; CoaBossLust.cpp feeds it the fight.
namespace CoaBossLustRule
{
    // The pull: the boss still has at least this much health.
    constexpr float PULL_HEALTH_PCT = 90.0f;

    enum class Verdict
    {
        Stock,   // the rule does not apply: the stock boost logic decides
        Allow,   // cast now
        Forbid,  // keep it for the boss
    };

    struct Situation
    {
        bool enabled = false;        // AiPlayerbot.CoaCombat.LustBossOnly
        bool grouped = false;
        bool encounterDecides = false;  // a raid or dungeon strategy times it for its own bosses
        bool bossInFight = false;    // a dungeon or world boss among the attackers
        float bossHealthPct = 100.0f;
        uint32_t thresholdPct = 30;  // AiPlayerbot.CoaCombat.LustBossHealthPct; 0 = at the pull only
        uint32_t lateBurstPct = 0;   // the boss wants it late only (LateBurstPct), 0 = none
    };

    // The debuffs that keep allies from a second burst: Worn Out (804457, every CoA class burst: Frenzy, Red Thirst,
    // Champion's Presence, Tempest's Call, Primal Awakening, Stellar Alignment), War Cry's Exhaustion (572126), and
    // the classic and CoA copies of Sated and Exhaustion.
    inline bool IsBurstDebuff(uint32_t spellId)
    {
        switch (spellId)
        {
            case 804457: case 572126: case 57723: case 57724: case 1157723: case 1157724:
                return true;
            default:
                return false;
        }
    }

    // A group burst: a spell that marks the allies it hasted with one of those debuffs, either as the aura it
    // checks on them or as a spell it triggers. Read from the spell data, so a new class burst is covered.
    inline bool IsGroupBurst(uint32_t excludeTargetAura, uint32_t const (&triggers)[3])
    {
        if (IsBurstDebuff(excludeTargetAura))
            return true;
        for (uint32_t trigger : triggers)
            if (IsBurstDebuff(trigger))
                return true;
        return false;
    }

    // A boss whose fight wants the burst at its end, not at the pull: Baron Geddon's last 10 % go before Armageddon.
    inline uint32_t LateBurstPct(std::string const& bossName)
    {
        return bossName == "Baron Geddon" ? 10u : 0u;
    }

    // The strategies that already time Bloodlust and Heroism for their bosses: the rule steps aside where they run.
    inline char const* const* EncounterStrategies()
    {
        static char const* const names[] = {"karazhan", "ssc",   "tempestkeep", "zulaman",
                                            "hyjal",    "blacktemple", "tbc-seth", nullptr};
        return names;
    }

    inline Verdict Decide(Situation const& s)
    {
        if (!s.enabled || !s.grouped || s.encounterDecides)
            return Verdict::Stock;
        if (!s.bossInFight)
            return Verdict::Forbid;
        if (s.lateBurstPct)
            return s.bossHealthPct <= static_cast<float>(s.lateBurstPct) ? Verdict::Allow : Verdict::Forbid;
        if (s.bossHealthPct >= PULL_HEALTH_PCT)
            return Verdict::Allow;
        if (s.thresholdPct && s.bossHealthPct <= static_cast<float>(s.thresholdPct))
            return Verdict::Allow;
        return Verdict::Forbid;
    }
}

#endif
