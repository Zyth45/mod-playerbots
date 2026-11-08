/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MCMultipliers.h"
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "DKActions.h"
#include "DruidActions.h"
#include "GenericSpellActions.h"
#include "HunterActions.h"
#include "MCActions.h"
#include "MCHelpers.h"
#include "MCTriggers.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "MovementActions.h"
#include "PaladinActions.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "ShamanActions.h"
#include "SpellAuras.h"
#include "WarriorActions.h"

using namespace MoltenCoreHelpers;

static bool IsDpsBotWithAoeAction(Player* bot, Action* action)
{
    if (PlayerbotAI::IsDps(bot))
    {
        if (dynamic_cast<DpsAoeAction*>(action) || dynamic_cast<CastConsecrationAction*>(action) ||
            dynamic_cast<CastStarfallAction*>(action) || dynamic_cast<CastWhirlwindAction*>(action) ||
            dynamic_cast<CastMagmaTotemAction*>(action) || dynamic_cast<CastExplosiveTrapAction*>(action) ||
            dynamic_cast<CastDeathAndDecayAction*>(action))
            return true;

        if (auto castSpellAction = dynamic_cast<CastSpellAction*>(action))
        {
            if (castSpellAction->getThreatType() == Action::ActionThreatType::Aoe)
                return true;
        }
    }
    return false;
}

float GarrDisableDpsAoeMultiplier::GetValue(Action* action)
{
    if (AI_VALUE2(Unit*, "find target", "garr"))
    {
        if (IsDpsBotWithAoeAction(bot, action))
            return 0.0f;
    }
    return 1.0f;
}

static bool IsAllowedGeddonMovementAction(Action* action)
{
    if (dynamic_cast<MovementAction*>(action) &&
                !dynamic_cast<McMoveFromGroupAction*>(action) &&
                !dynamic_cast<McMoveFromBaronGeddonAction*>(action))
        return false;

    if (dynamic_cast<CastReachTargetSpellAction*>(action))
        return false;

    return true;
}

float BaronGeddonAbilityMultiplier::GetValue(Action* action)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "baron geddon");
    if (boss && IsInfernoActive(boss))
    {
        if (!IsAllowedGeddonMovementAction(action))
            return 0.0f;
    }

    // No check for Baron Geddon, because bots may have the bomb even after Geddon died.
    // CoA: the bomb's 1 s dummy cast aimed at this bot counts as well.
    if (HasAnyAura(bot, LIVING_BOMB_AURAS) || (boss && IsLivingBombCastAt(boss, bot)))
    {
        if (!IsAllowedGeddonMovementAction(action))
            return 0.0f;
    }

    return 1.0f;
}

static bool IsSingleLivingTankInGroup(Player* bot)
{
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member || !member->IsAlive() || member == bot)
                continue;
            if (PlayerbotAI::IsTank(member))
                return false;
        }
    }
    return true;
}

float GolemaggMultiplier::GetValue(Action* action)
{
    if (Unit* golemagg = AI_VALUE2(Unit*, "find target", "golemagg the incinerator"))
    {
        // Below 10% (soft enrage: Earthquake + converging Core Ragers) the
        // fight is a burn race, everyone dps, no splash management.
        bool burnPhase = golemagg->GetHealthPct() <= 10.0f;
        if (PlayerbotAI::IsTank(bot) && IsSingleLivingTankInGroup(bot))
        {
            // Only one tank => Pick up Golemagg and the two Core Ragers
            if (dynamic_cast<McGolemaggMainTankAttackGolemaggAction*>(action) ||
                dynamic_cast<McGolemaggAssistTankAttackCoreRagerAction*>(action))
                return 0.0f;
        }
        if (PlayerbotAI::IsAssistTank(bot))
        {
            // The first two assist tanks manage the Core Ragers. The remaining assist tanks attack the boss.
            if (dynamic_cast<TankAssistAction*>(action))
                return 0.0f;
        }
        if (IsDpsBotWithAoeAction(bot, action))
            return 0.0f;

        // Golemagg's Magma Splash stacks an uncapped 30s dot on anyone striking
        // him in melee. This blocks the default "melee when out of mana / in dead zone" fallback
        // that walks casters and hunters onto the boss.
        if (!burnPhase && PlayerbotAI::IsRanged(bot) && dynamic_cast<MeleeAction*>(action))
            return 0.0f;

        // Backed-off melee stay out until their whole Magma Splash stack expires (30s after the last application).
        bool backedOff = GetAuraStacks(bot, MAGMA_SPLASH_AURAS) >= MAGMA_SPLASH_BACK_OFF_STACKS;
        bool engagesBoss = !dynamic_cast<McGolemaggBackOffAction*>(action) &&
                           (dynamic_cast<AttackAction*>(action) || dynamic_cast<MeleeAction*>(action) ||
                            dynamic_cast<CastReachTargetSpellAction*>(action));
        if (!burnPhase && !PlayerbotAI::IsTank(bot) && backedOff && engagesBoss)
            return 0.0f;
    }
    return 1.0f;
}

float MajordomoReflectionMultiplier::GetValue(Action* action)
{
    // Majordomo shields his adds for 10 s: Magic Reflection sends spells back at the caster, Damage Shield
    // burns every melee hit. Bots hold those attacks on their target until it fades; tanks keep the adds.
    if (PlayerbotAI::IsTank(bot))
        return 1.0f;

    bool const isAttack = dynamic_cast<AttackAction*>(action) != nullptr;
    auto* spellAction = dynamic_cast<CastSpellAction*>(action);
    if (!isAttack && !spellAction)
        return 1.0f;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (!IsMajordomoAdd(target))
        return 1.0f;

    bool const reflects = target->HasAura(SPELL_MAGIC_REFLECTION);
    bool const shielded = target->HasAura(SPELL_DAMAGE_SHIELD);
    if (!reflects && !shielded)
        return 1.0f;

    // Only melee swings hit the shield; ranged auto-attacks do not.
    if (isAttack)
        return shielded && PlayerbotAI::IsMelee(bot) ? 0.0f : 1.0f;

    if (spellAction->GetTargetName() != "current target")
        return 1.0f;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(AI_VALUE2(uint32, "spell id", spellAction->getSpell()));
    if (!spellInfo)
        return 1.0f;

    // The core's own tests: Spell::m_canReflect, and Unit::DealDamage for Damage Shield procs.
    auto const canReflect = [](SpellInfo const* info)
    {
        return info && info->DmgClass == SPELL_DAMAGE_CLASS_MAGIC && !info->HasAttribute(SPELL_ATTR0_IS_ABILITY) &&
               !info->HasAttribute(SPELL_ATTR1_NO_REFLECTION) && !info->HasAttribute(SPELL_ATTR0_NO_IMMUNITIES) &&
               !info->IsPassive() && (!info->IsPositive() || info->HasEffect(SPELL_EFFECT_DISPEL));
    };
    if (reflects)
    {
        if (canReflect(spellInfo))
            return 0.0f;

        // A channel like Arcane Missiles is not reflected itself, but the spells it triggers are.
        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (effect.TriggerSpell && canReflect(sSpellMgr->GetSpellInfo(effect.TriggerSpell)))
                return 0.0f;
        }
    }

    if (shielded && spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MELEE &&
        !(spellInfo->GetSchoolMask() & SPELL_SCHOOL_MASK_HOLY))
        return 0.0f;

    return 1.0f;
}

float ShazzrahReflectionMultiplier::GetValue(Action* action)
{
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsRanged(bot))
        return 1.0f;

    Unit* boss = AI_VALUE2(Unit*, "find target", "shazzrah");
    if (!boss || !IsShazzrahHeldByTank(boss) ||
        !FindReflectionNear(GetShazzrahReflections(boss), boss, REFLECTION_DISTANCE))
        return 1.0f;

    bool const engagesBoss = !dynamic_cast<McMoveFromShazzrahReflectionAction*>(action) &&
                             (dynamic_cast<AttackAction*>(action) || dynamic_cast<MeleeAction*>(action) ||
                              dynamic_cast<CastReachTargetSpellAction*>(action));
    return engagesBoss ? 0.0f : 1.0f;
}

float ShazzrahSaveInterruptMultiplier::GetValue(Action* action)
{
    constexpr uint32 SPELL_COA_ARCANE_FORCE_NOVA = 2105612;
    auto* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !McCoaFixesOn())
        return 1.0f;

    Unit* target = cast->GetTarget();
    if (!target || !target->IsAlive() || !IsMcEntry(target->GetEntry(), 12264))  // Shazzrah
        return 1.0f;
    Spell* casting = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!casting || !casting->m_spellInfo || casting->m_spellInfo->Id == SPELL_COA_ARCANE_FORCE_NOVA)
        return 1.0f;

    // Only an interrupt is held back: a spell of this bot that interrupts a cast.
    uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    return info && info->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) ? 0.0f : 1.0f;
}

float RagnarosSonsNoAoeMultiplier::GetValue(Action* action)
{
    if (!McCoaFixesOn() || bot->GetMapId() != 409 || !IsDpsBotWithAoeAction(bot, action))
        return 1.0f;
    return FindSonOfFlameToKill(bot) ? 0.0f : 1.0f;
}
