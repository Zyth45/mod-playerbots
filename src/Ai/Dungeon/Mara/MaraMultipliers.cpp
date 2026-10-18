/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MaraMultipliers.h"

#include "AiObjectContext.h"
#include "MaraActions.h"
#include "MaraTriggers.h"
#include "MovementActions.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace Maraudon;

// While the boulder is cast nothing else moves the bot, "coa boss dodge" included: it would keep the casters
// out of Trample, away from the share.
float LandslideMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<MaraShareBoulderAction*>(action) || !dynamic_cast<MovementAction*>(action))
        return 1.0f;

    Unit* landslide = AI_VALUE2(Unit*, "find target", "landslide");
    Unit* target = landslide && landslide->IsAlive() ? CastTarget(landslide, "Hurl Boulder") : nullptr;
    return target && target != bot ? 0.0f : 1.0f;
}

float TheradrasMultiplier::GetValue(Action* action)
{
    if (botAI->IsTank(bot) || !MovesIntoDanger(action))
        return 1.0f;

    return TheradrasDusting(botAI, context) ? 0.0f : 1.0f;
}
