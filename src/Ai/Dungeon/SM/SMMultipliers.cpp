/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SMMultipliers.h"
#include "GenericSpellActions.h"
#include "MovementActions.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SMActions.h"
#include "SMShared.h"

using namespace SMShared;

float ArcanistDoanDetonationMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<ArcanistDoanMoveOutOfDetonationAction*>(action))
        return 1.0f;

    bool const isMovement =
        dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action);
    bool const isSpell = dynamic_cast<CastSpellAction*>(action);
    if (!isMovement && !isSpell)
        return 1.0f;

    Unit* doan = AI_VALUE2(Unit*, "find target", "arcanist doan");
    if (!doan || !IsDetonationImminent(doan))
        return 1.0f;

    // Nothing may walk a bot back toward Doan until Detonation has gone off.
    if (isMovement)
        return 0.0f;

    // Inside the blast radius, standing still to cast is how bots die; outside it they may keep casting.
    return bot->GetExactDist2d(doan) < DETONATION_SAFE_DISTANCE ? 0.0f : 1.0f;
}
