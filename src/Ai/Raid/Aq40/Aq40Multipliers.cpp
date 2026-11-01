/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Multipliers.h"
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
