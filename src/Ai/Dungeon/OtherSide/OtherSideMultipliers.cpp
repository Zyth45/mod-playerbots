/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OtherSideMultipliers.h"
#include "MovementActions.h"
#include "OtherSideActions.h"
#include "OtherSideShared.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"

using namespace OtherSideShared;

float OtherSideHazardMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<OtherSideLeaveHazardAction*>(action))
        return 1.0f;

    bool const isMovement =
        dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action);
    if (!isMovement)
        return 1.0f;

    // Standing in a hazard, the only way to go is out of it.
    if (InHazard(bot))
        return 0.0f;

    // In the egg phase only the run and the eggs on its way move a bot.
    if (InEggPhase(bot))
        return dynamic_cast<OtherSideKickEggAction*>(action) || dynamic_cast<OtherSideRunAroundAction*>(action)
            ? 1.0f : 0.0f;

    return 1.0f;
}
