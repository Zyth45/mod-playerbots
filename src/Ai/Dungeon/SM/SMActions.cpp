/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SMActions.h"
#include "Playerbots.h"
#include "SMShared.h"

using namespace SMShared;

bool ArcanistDoanMoveOutOfDetonationAction::Execute(Event /*event*/)
{
    Unit* doan = AI_VALUE2(Unit*, "find target", "arcanist doan");
    if (!doan)
        return false;

    float const distance = bot->GetExactDist2d(doan);
    if (distance >= DETONATION_SAFE_DISTANCE)
        return false;

    bot->CastStop();
    return MoveAway(doan, DETONATION_SAFE_DISTANCE - distance);
}
