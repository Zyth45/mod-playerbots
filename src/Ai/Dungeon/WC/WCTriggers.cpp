/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WCTriggers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace WailingCaverns
{
    bool IsOverrunning(Unit const* skum)
    {
        return skum && skum->IsAlive() && IsCastingNamed(skum, "Overrun");
    }

    float OverrunHalfWidth(Unit const* skum, Unit const* bot)
    {
        // The trample's range check counts both bodies.
        return OVERRUN_RADIUS + skum->GetBoundaryRadius() + bot->GetBoundaryRadius() + OVERRUN_MARGIN;
    }

    bool InOverrunPath(Unit const* skum, Unit const* bot)
    {
        return InPath(skum, skum->GetOrientation(), OVERRUN_LENGTH, OverrunHalfWidth(skum, bot), bot);
    }
}

using namespace WailingCaverns;

bool WcSkumOverrunTrigger::IsActive()
{
    Unit* skum = AI_VALUE2(Unit*, "find target", "skum");
    return IsOverrunning(skum) && InOverrunPath(skum, bot);
}
