/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Multipliers.h"
#include "Aq20Utils.h"
#include "Aq40Triggers.h"
#include "GenericSpellActions.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Aq40Helpers.h"
#include "AttackAction.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"

using namespace Aq40Helpers;

float Aq40CthunMultiplier::GetValue(Action* action)
{
    if (!dynamic_cast<AttackAction*>(action) && !dynamic_cast<CastSpellAction*>(action))
        return 1.0f;
    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;
    if (!IsCthunEncounter(bot))
        return 1.0f;

    Unit* target = action->GetTarget();

    // Eldritch Revelations: attacking or casting at a target turns the bot
    // toward it, and facing the caster during a pulse stuns for 8s.
    if (target && target != bot && GetEldritchCaster(bot))
        return 0.0f;

    // Impenetrable Force Shield: -99% damage taken outside the Weakening.
    if (IsCthunBody(target) && !IsCthunWeakened(bot))
        return 0.0f;

    return 1.0f;
}

float Aq40SkeramMultiplier::GetValue(Action* action)
{
    if (!dynamic_cast<AttackAction*>(action) && !dynamic_cast<CastSpellAction*>(action))
        return 1.0f;
    if (dynamic_cast<CastHealingSpellAction*>(action))
        return 1.0f;
    if (!IsSkeramEncounter(bot))
        return 1.0f;

    Unit* target = action->GetTarget();
    if (!IsSkeram(target))
        return 1.0f;

    // Nightmare Veil: immune while any nightmare stands.
    if (target->HasAura(SPELL_NIGHTMARE_VEIL))
        return 0.0f;

    // True Fulfillment: a quarter of the damage goes to the worshipper.
    if (!PlayerbotAI::IsTank(bot) && ShouldHoldOnSkeram(bot))
        return 0.0f;

    return 1.0f;
}

float Aq40SarturaInterruptMultiplier::GetValue(Action* action)
{
    auto* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast || !AqCoaFixesOn())
        return 1.0f;
    Unit* target = cast->GetTarget();
    if (!target || target->GetEntry() != CoaAq40::NPC_SARTURA)
        return 1.0f;
    Aura const* frustration = target->GetAura(CoaAq40::SPELL_FRUSTRATION);
    if (!frustration || frustration->GetStackAmount() < CoaAq40::FRUSTRATION_HOLD_STACKS)
        return 1.0f;
    uint32 const spellId = AI_VALUE2(uint32, "spell id", cast->getSpell());
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    return info && info->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) ? 0.0f : 1.0f;
}
