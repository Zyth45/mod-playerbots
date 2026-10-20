/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaDungeonPriorities.h"

#include "CoaBossKnowledge.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Log.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <array>
#include <atomic>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace CoaDungeonPriorities
{
namespace
{
/*
 * Classic dungeon boss casts to kick before any other, from the CoA boss ability list of 09/10
 * (bot-boss-tactics.md, 19 dungeons). That list is generated and noisy, so only the casts whose
 * effect is plainly a heal, crowd control or a big group hit are kept: plain bolts are left to the
 * generic rule, and so are oddities such as Bly's Band's Escape (11365), a hearthstone.
 */
constexpr uint32 PriorityCasts[] = {
    // Heals and drains that heal the caster
    15493, 13952, 2102661,                 // Holy Light
    10917,                                 // Flash Heal
    8362,                                  // Renew
    15586, 12039, 381459,                  // Heal
    15585,                                 // Prayer of Healing
    2102590,                               // Cookie's Cooking
    2100069,                               // Regrowth
    7948,                                  // Wild Regeneration
    15982, 12492, 12491,                   // Healing Wave
    11895,                                 // Healing Wave of Antu'sul
    2102782,                               // Healing Stream
    2102601,                               // Healing Touch
    17233,                                 // Lay on Hands
    115010, 20743,                         // Drain Life
    376710,                                // Soul Siphon
    2102647,                               // Siphon Life
    7295,                                  // Soul Drain
    // Crowd control on players
    2102600, 8399, 12098,                  // Sleep
    16798,                                 // Enchanting Lullaby
    12890,                                 // Deep Slumber
    8040,                                  // Druid's Slumber
    8994,                                  // Banish
    2100019, 13323,                        // Polymorph
    12096, 2100063,                        // Fear
    2100244, 377065, 2102779,              // Dominate Mind
    2102679,                               // Curse of Arugal (mind control)
    92624,                                 // Krastinov's Ghoulish Potion (mind control)
    8988, 2100287, 18327,                  // Silence
    2102660,                               // Hammer of Justice
    16497,                                 // Stun Bomb
    16075,                                 // Throw Axe (stun)
    17293,                                 // Burning Winds (stun)
    16869,                                 // Ice Tomb
    11836,                                 // Freeze Solid
    2101111,                               // Frost Nova
    11876, 381296,                         // War Stomp
    2100185, 2102635,                      // Mass Entanglement
    22924, 8142,                           // Grasping Vines
    12747,                                 // Entangling Roots
    2100215,                               // Sacrifice
    2102208,                               // Conflagration
    // Big hits on the whole group
    15245, 2100055, 2102781, 2090114, 2100054, 2090112,  // Shadow Bolt Volley
    17203, 22425,                          // Fireball Volley
    8398, 2100368,                         // Frostbolt Volley
    2102834, 2102186,                      // Pyroblast
    2102791, 11082,                        // Mega Volt, Megavolt
    2102774,                               // Lightning Nova
    9435,                                  // Detonation
    21793,                                 // Twisted Tranquility
    2100085,                               // Call of the Grave
    // Summons
    13895,                                 // Summon Spawn of Bael'Gar
    2102550,                               // Rain of Fire (summons its fire)
    11899, 381319,                         // Healing Ward
};

// Spells that summon an add the group should kill before the boss: the add is the creature the
// spell summons, read from its summon effect, so that no creature entry is written by hand.
constexpr uint32 PriorityAddSummons[] = {
    11899, 381319,  // Healing Ward (Greater Healing Ward heals the boss)
    8376, 2102771,  // Earthgrab Totem (roots the group)
    11086,          // Ward of Zum'rah (raises skeletons)
    12506,          // Atal'ai Skeleton Totem (raises skeletons)
};

struct Tables
{
    std::unordered_set<uint32> casts;
    std::unordered_set<uint32> adds;  // creature entries
};

// Built on first use, once the spell store is loaded: a magic static, so thread safe.
Tables const& Get()
{
    static Tables const tables = []
    {
        Tables built;
        uint32 unknownCasts = 0, unknownSummons = 0;
        for (uint32 const id : PriorityCasts)
        {
            if (sSpellMgr->GetSpellInfo(id))
                built.casts.insert(id);
            else
                ++unknownCasts;
        }

        for (uint32 const id : PriorityAddSummons)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info)
            {
                ++unknownSummons;
                continue;
            }
            for (SpellEffectInfo const& effect : info->GetEffects())
                if (effect.Effect == SPELL_EFFECT_SUMMON && effect.MiscValue > 0)
                    built.adds.insert(uint32(effect.MiscValue));
        }

        LOG_INFO("playerbots", "coa dungeon: {} priority casts ({} unknown to the server), {} priority adds from {} summons "
                 "({} unknown)", built.casts.size(), unknownCasts, built.adds.size(), std::size(PriorityAddSummons),
                 unknownSummons);
        return built;
    }();
    return tables;
}

// Whether the cast keeps the enemy side alive or takes a player out of the fight, looking one level
// into the spells it triggers.
bool HealsOrControls(SpellInfo const* info, uint8 depth)
{
    switch (info->Mechanic)
    {
        case MECHANIC_CHARM: case MECHANIC_DISORIENTED: case MECHANIC_FEAR: case MECHANIC_ROOT:
        case MECHANIC_SILENCE: case MECHANIC_SLEEP: case MECHANIC_STUN: case MECHANIC_FREEZE:
        case MECHANIC_POLYMORPH: case MECHANIC_BANISH: case MECHANIC_HORROR:
            return true;
        default:
            break;
    }

    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (!effect.IsEffect())
            continue;

        switch (effect.Effect)
        {
            case SPELL_EFFECT_HEAL: case SPELL_EFFECT_HEAL_PCT: case SPELL_EFFECT_HEAL_MAX_HEALTH:
            case SPELL_EFFECT_HEALTH_LEECH: case SPELL_EFFECT_SUMMON:
                return true;
            default:
                break;
        }

        if (effect.IsAura())
        {
            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_PERIODIC_HEAL: case SPELL_AURA_PERIODIC_LEECH: case SPELL_AURA_MOD_STUN:
                case SPELL_AURA_MOD_FEAR: case SPELL_AURA_MOD_CONFUSE: case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_MOD_POSSESS: case SPELL_AURA_AOE_CHARM: case SPELL_AURA_MOD_SILENCE:
                case SPELL_AURA_MOD_PACIFY_SILENCE: case SPELL_AURA_MOD_ROOT:
                    return true;
                default:
                    break;
            }
        }

        if (effect.TriggerSpell && effect.TriggerSpell != info->Id && depth < 1)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (HealsOrControls(triggered, depth + 1))
                    return true;
    }
    return false;
}

bool OnAllies(SpellEffectInfo const& effect)
{
    for (SpellImplicitTargetInfo const& target : { effect.TargetA, effect.TargetB })
        switch (target.GetCheckType())
        {
            case TARGET_CHECK_ALLY: case TARGET_CHECK_PARTY: case TARGET_CHECK_RAID: case TARGET_CHECK_RAID_CLASS:
                return true;
            default:
                break;
        }
    return false;
}

// Whether the spell heals or shields the caster's allies, a ward's pulse included (its aura casts
// the heal every few seconds). A heal only for the caster does not count: that creature only
// lasts longer, it keeps nobody else up.
bool SupportsAllies(SpellInfo const* info, uint8 depth)
{
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (!effect.IsEffect())
            continue;

        bool const allies = OnAllies(effect);
        if (allies && (effect.Effect == SPELL_EFFECT_HEAL || effect.Effect == SPELL_EFFECT_HEAL_PCT ||
                       effect.Effect == SPELL_EFFECT_HEAL_MAX_HEALTH))
            return true;

        if (allies && effect.IsAura() &&
            (effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL || effect.ApplyAuraName == SPELL_AURA_SCHOOL_ABSORB ||
             effect.ApplyAuraName == SPELL_AURA_SCHOOL_IMMUNITY || effect.ApplyAuraName == SPELL_AURA_DAMAGE_IMMUNITY))
            return true;

        if (effect.TriggerSpell && effect.TriggerSpell != info->Id && depth < 1)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (SupportsAllies(triggered, depth + 1))
                    return true;
    }
    return false;
}

bool EntrySupportsAllies(Creature* creature)
{
    CreatureTemplate const* proto = creature->GetCreatureTemplate();
    if (!proto)
        return false;

    for (uint32 const id : proto->spells)
        if (id)
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(id))
                if (SupportsAllies(info, 0))
                    return true;

    // What its smart_scripts rows cast, when AiPlayerbot.CoaBossKnowledge loaded them.
    if (std::vector<uint32> const* known = CoaBossKnowledge::KnownSpells(creature->GetEntry()))
        for (uint32 const id : *known)
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(id))
                if (SupportsAllies(info, 0))
                    return true;

    return false;
}

std::mutex SupportCacheLock;
std::unordered_map<uint32, bool> SupportCache;  // creature entry -> heals or shields its allies

std::array<std::atomic<uint32>, size_t(Decision::Max)> LastLog{};
}  // namespace

uint8 InterruptRank(SpellInfo const* info)
{
    if (!info)
        return 0;
    if (Get().casts.count(info->Id))
        return 2;
    return HealsOrControls(info, 0) ? 1 : 0;
}

bool HealsOrProtects(Creature* creature)
{
    if (!creature)
        return false;
    if (creature->IsTotem())
        return true;
    if (creature->IsDungeonBoss() || creature->isWorldBoss())
        return false;
    if (Get().adds.count(creature->GetEntry()))
        return true;

    // Several map threads ask; the answer only depends on the entry.
    std::lock_guard<std::mutex> guard(SupportCacheLock);
    auto const found = SupportCache.find(creature->GetEntry());
    if (found != SupportCache.end())
        return found->second;
    bool const supports = EntrySupportsAllies(creature);
    SupportCache.emplace(creature->GetEntry(), supports);
    return supports;
}

bool LogDue(Decision decision)
{
    constexpr uint32 LogEveryMs = 60 * IN_MILLISECONDS;
    std::atomic<uint32>& last = LastLog[size_t(decision)];
    uint32 const now = getMSTime();
    uint32 seen = last.load();
    if (seen && getMSTimeDiff(seen, now) < LogEveryMs)
        return false;
    return last.compare_exchange_strong(seen, now ? now : 1);
}
}
