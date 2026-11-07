/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WCMultipliers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"
#include "WCTriggers.h"

using namespace DungeonBoss;
using namespace WailingCaverns;

float SkumMultiplier::GetValue(Action* action)
{
    if (!MovesIntoDanger(action))
        return 1.0f;

    // Walking back in to Skum from the side of his path leads straight into it, while he holds still for the cast
    // and while he charges and tramples after it.
    Unit* skum = AI_VALUE2(Unit*, "find target", "skum");
    return ActiveOverrun(skum) ? 0.0f : 1.0f;
}
