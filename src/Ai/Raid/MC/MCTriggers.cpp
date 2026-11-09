/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MCTriggers.h"
#include "MCHelpers.h"
#include "SharedDefines.h"
#include "Group.h"
#include "Playerbots.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <climits>

using namespace MoltenCoreHelpers;

bool McLivingBombDebuffTrigger::IsActive()
{
    // No check for Baron Geddon, because bots may have the bomb even after Geddon died.
    if (HasAnyAura(bot, LIVING_BOMB_AURAS))
        return true;

    // CoA: the debuff follows a 1 s dummy cast at its target, so the carrier
    // can already leave during the cast bar.
    Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon");
    return boss && IsLivingBombCastAt(boss, bot);
}

bool McBaronGeddonInfernoTrigger::IsActive()
{
    if (Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon"))
        return IsInfernoActive(boss);
    return false;
}

bool McShazzrahRangedTrigger::IsActive()
{
    // Only fire inside Arcane Explosion range. The move-away action no-ops
    // beyond it, so an unconditional trigger makes every already-safe ranged
    // bot attempt a failing move each tick.
    if (!PlayerbotAI::IsRanged(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "shazzrah");
    return boss && bot->GetDistance2d(boss) < ARCANE_EXPLOSION_DISTANCE;
}

bool McInLavaTrigger::IsActive()
{
    LiquidData const& liquid = bot->GetLiquidData();
    return (liquid.Flags & MAP_LIQUID_TYPE_MAGMA) &&
           (liquid.Status & (LIQUID_MAP_WATER_WALK | LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER));
}

bool McGolemaggMagmaSplashTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
    if (!boss)
        return false;

    // Below 10% the fight is a burn race (Earthquake pulses, Core Ragers
    // converge): stack management stops mattering, melee return to dps.
    if (boss->GetHealthPct() <= 10.0f)
        return false;

    uint32 const stacks =
        GetAuraStacks(bot, MAGMA_SPLASH_AURAS) + (McCoaFixesOn() ? GetAuraStacks(bot, MAGMA_SPLASH_COA_AURAS) : 0);
    if (stacks < MAGMA_SPLASH_BACK_OFF_STACKS)
        return false;

    // Only fire while still inside swing range; once the bot has backed off,
    // the multiplier keeps it from re-engaging until the stack expires.
    return bot->GetDistance2d(boss) < MAGMA_SPLASH_BACK_OFF_DISTANCE;
}

bool McGolemaggMarkBossTrigger::IsActive()
{
    // any tank may mark the boss
    return PlayerbotAI::IsTank(bot) && AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
}

bool McGolemaggIsMainTankTrigger::IsActive()
{
    return PlayerbotAI::IsMainTank(bot) && AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
}

bool McGolemaggIsAssistTankTrigger::IsActive()
{
    return PlayerbotAI::IsAssistTank(bot) && AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
}

bool McGolemaggIsHealerTrigger::IsActive()
{
    return PlayerbotAI::IsHeal(bot) && AI_VALUE2(Unit*, "find target", "golemagg the incinerator");
}

bool McGehennasFlameOrbCastTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "gehennas");
    return boss && boss->IsAlive() && boss->FindCurrentSpellBySpellId(SPELL_CONJURE_FLAME_ORB_COA) &&
           bot->GetDistance2d(boss) < FLAME_ORB_CAST_CLEAR_DISTANCE;
}

bool McGehennasRainOfFireTrigger::IsActive()
{
    return HasAnyAura(bot, GEHENNAS_RAIN_OF_FIRE_AURAS);
}

bool McMagmadarLavaBombTrigger::IsActive()
{
    return HasAnyAura(bot, MAGMADAR_LAVA_BOMB_AURAS);
}

bool McGeddonBurningGroundTrigger::IsActive()
{
    return McCoaFixesOn() && HasAnyAura(bot, GEDDON_BURNING_GROUND_AURAS);
}

namespace
{
    // The spell of this bot that removes `type` from an ally (by its effect, so CoA classes are covered), or 0.
    uint32 AllyDispelSpell(Player* bot, uint32 type)
    {
        uint32 best = 0;
        uint32 bestLevel = 0;
        for (auto const& [spellId, spell] : bot->GetSpellMap())
        {
            if (!spell || spell->State == PLAYERSPELL_REMOVED || !spell->Active)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (!info || info->IsPassive())
                continue;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                SpellEffectInfo const& effect = info->Effects[i];
                if (effect.Effect != SPELL_EFFECT_DISPEL || effect.MiscValue != int32(type))
                    continue;
                Targets const target = effect.TargetA.GetTarget();
                if (target != TARGET_UNIT_TARGET_ALLY && target != TARGET_UNIT_TARGET_ANY &&
                    target != TARGET_UNIT_TARGET_RAID && target != TARGET_UNIT_TARGET_PARTY)
                    continue;
                if (!best || info->SpellLevel > bestLevel)
                {
                    best = spellId;
                    bestLevel = info->SpellLevel;
                }
            }
        }
        return best;
    }
}

Player* McRaidDispelTarget(Player* bot, uint32& spellId)
{
    spellId = 0;
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    bool const healer = PlayerbotAI::IsHeal(bot);
    uint32 removal[DISPEL_POISON + 1];
    bool resolved[DISPEL_POISON + 1] = {};
    Player* best = nullptr;
    int bestRank = INT_MAX;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !member->IsInWorld() || member->GetMapId() != bot->GetMapId() ||
            bot->GetDistance(member) > 40.0f)
            continue;
        int const rank = PlayerbotAI::IsTank(member) ? 0 : PlayerbotAI::IsHeal(member) ? 1 : 2;
        if (rank >= bestRank || (healer && rank != 0))
            continue;
        for (uint32 auraId : MC_RAID_DISPEL_AURAS)
        {
            Aura* aura = member->GetAura(auraId);
            if (!aura)
                continue;
            uint32 const type = aura->GetSpellInfo()->Dispel;
            if (type != DISPEL_CURSE && type != DISPEL_MAGIC)
                continue;
            if (!resolved[type])
            {
                removal[type] = AllyDispelSpell(bot, type);
                resolved[type] = true;
            }
            if (!removal[type])
                continue;
            best = member;
            bestRank = rank;
            spellId = removal[type];
            break;
        }
    }
    return best;
}

bool McRaidDispelTrigger::IsActive()
{
    if (!McCoaFixesOn() || bot->GetMapId() != 409 || !bot->IsInCombat())
        return false;
    uint32 spellId = 0;
    return McRaidDispelTarget(bot, spellId) != nullptr;
}

bool McCoreHoundMarkTrigger::IsActive()
{
    // Magmadar's hounds cannot die while he lives: the skull stays on him.
    return PlayerbotAI::IsMainTank(bot) && AI_VALUE2(Unit*, "find target", "core hound") &&
           !AI_VALUE2(Unit*, "find target", "magmadar");
}

bool McMajordomoDamageShieldTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot) || !PlayerbotAI::IsMelee(bot))
        return false;

    Unit* victim = bot->GetVictim();
    return IsMajordomoAdd(victim) && victim->HasAura(SPELL_DAMAGE_SHIELD);
}

bool McShazzrahReflectionTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "shazzrah");
    if (!boss)
        return false;

    std::vector<Creature*> const reflections = GetShazzrahReflections(boss);
    if (!FindReflectionNear(reflections, bot, REFLECTION_DISTANCE))
        return false;

    // Melee cannot reach a boss that stands in a Reflection from outside it; with no tank to walk him out
    // they stay on him rather than leave the fight.
    return PlayerbotAI::IsRanged(bot) || IsShazzrahHeldByTank(boss) ||
           !FindReflectionNear(reflections, boss, REFLECTION_DISTANCE);
}

bool McShazzrahTankReflectionTrigger::IsActive()
{
    if (!PlayerbotAI::IsTank(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "shazzrah");
    if (!boss || boss->GetVictim() != bot)
        return false;

    std::vector<Creature*> const reflections = GetShazzrahReflections(boss);
    return FindReflectionNear(reflections, boss, REFLECTION_DISTANCE) ||
           FindReflectionNear(reflections, bot, REFLECTION_DISTANCE);
}

bool McSacrificialChainsTrigger::IsActive()
{
    // Healers keep the raid alive and tanks keep Majordomo's adds; chained bots cannot act; a bot already
    // on a chain keeps its rotation going.
    if (PlayerbotAI::IsHeal(bot) || PlayerbotAI::IsTank(bot) || bot->HasUnitFlag(UNIT_FLAG_PACIFIED) ||
        IsSacrificialChains(bot->GetVictim()))
        return false;

    Unit* majordomo = AI_VALUE2(Unit*, "find target", "majordomo executus");
    return majordomo && FindSacrificialChains(majordomo, bot) != nullptr;
}

bool McBaronGeddonBurnTrigger::IsActive()
{
    constexpr float BURN_HEALTH_PCT = 10.0f;
    if (!McCoaFixesOn() || bot->getClass() != CLASS_SHAMAN || !bot->IsInCombat())
        return false;
    Unit* geddon = AI_VALUE2(Unit*, "find target", "baron geddon");
    return geddon && geddon->IsAlive() && geddon->GetHealthPct() <= BURN_HEALTH_PCT;
}

bool McGolemaggCaveInTrigger::IsActive()
{
    return McCoaFixesOn() && HasAnyAura(bot, GOLEMAGG_CAVE_IN_AURAS);
}

bool McRagnarosSonsTrigger::IsActive()
{
    return McCoaFixesOn() && PlayerbotAI::IsMainTank(bot) && FindSonOfFlameToKill(bot);
}

bool McRagnarosMagmaStrikeTrigger::IsActive()
{
    return McCoaFixesOn() && HasAnyAura(bot, RAGNAROS_MAGMA_STRIKE_AURAS);
}

bool McGarrFireswornMarkTrigger::IsActive()
{
    return McCoaFixesOn() && PlayerbotAI::IsMainTank(bot) && FindFireswornToKill(bot);
}

bool McGarrFireswornEruptionTrigger::IsActive()
{
    // Tanks hold their add through the Eruption; the melee damage dealers step out and the ranged finish it.
    return McCoaFixesOn() && !PlayerbotAI::IsTank(bot) && PlayerbotAI::IsMelee(bot) &&
           FindDyingFireswornNear(bot, FIRESWORN_ERUPTION_YARDS + 1.0f);
}
