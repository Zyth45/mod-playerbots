/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFDMultipliers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"
#include "RFDTriggers.h"

using namespace DungeonBoss;
using namespace RazorfenDowns;

// Melee and the tank walk back in only once the cast or the storm is over.
float GluttonMultiplier::GetValue(Action* action)
{
    return MovesIntoDanger(action) && GluttonExpelling(botAI, context) ? 0.0f : 1.0f;
}

float MordreshFireEyeMultiplier::GetValue(Action* action)
{
    return MovesIntoDanger(action) && MordreshStorming(botAI, context) ? 0.0f : 1.0f;
}
