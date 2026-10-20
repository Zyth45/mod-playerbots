/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "InvalidTargetValue.h"
#include "AttackersValue.h"
#include "Playerbots.h"
#include "Unit.h"

bool InvalidTargetValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    Unit* enemy = AI_VALUE(Unit*, "enemy player target");
    if (target && enemy && target == enemy && target->IsAlive())
        return false;

    if (target && qualifier == "current target")
    {
        return target->GetMapId() != bot->GetMapId() || target->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) ||
               target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) || target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE_2) ||
               // A feared creature is hit like any other: the bots dropped it as their target and let
               // it run, and CoA puts fear on many rotations (reported in game, 27/09).
               !target->IsVisible() || !target->IsAlive() || target->IsPolymorphed() || target->IsCharmed() ||
               target->HasUnitState(UNIT_STATE_ISOLATED) || target->IsFriendlyTo(bot) ||
               !(sPlayerbotAIConfig.coaKeepTargetOutOfSight ? AttackersValue::IsPossibleTarget(target, bot)
                                                             : AttackersValue::IsValidTarget(target, bot));
    }

    return !target;
}
