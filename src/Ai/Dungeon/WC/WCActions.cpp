/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WCActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace WailingCaverns;

bool WcMoveFromOverrunAction::Execute(Event /*event*/)
{
    Unit* skum = AI_VALUE2(Unit*, "find target", "skum");
    if (!IsOverrunning(skum) || !InOverrunPath(skum, bot))
        return false;

    return MoveOutOfPath(skum, skum->GetOrientation(), OverrunHalfWidth(skum, bot), OVERRUN_SIDE_STEP);
}
