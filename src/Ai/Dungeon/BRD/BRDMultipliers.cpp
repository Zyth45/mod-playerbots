/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRDMultipliers.h"

#include "AiObjectContext.h"
#include "BRDTriggers.h"
#include "ChooseTargetActions.h"
#include "Playerbots.h"

using namespace BlackrockDepths;

namespace
{
    bool IsAssist(Action* action) { return dynamic_cast<DpsAssistAction*>(action) != nullptr; }
}

// "Attack what the tank attacks" would pull the casters straight back onto Flamelash.
float AmbassadorFlamelashMultiplier::GetValue(Action* action)
{
    if (!IsAssist(action) || !botAI->IsRangedDps(bot))
        return 1.0f;

    return BurningSpiritToKill(botAI, context, bot) ? 0.0f : 1.0f;
}

float ThaurissanMultiplier::GetValue(Action* action)
{
    if (!IsAssist(action) || botAI->IsTank(bot) || botAI->IsHeal(bot))
        return 1.0f;

    return ThaurissanToFocus(botAI, context) ? 0.0f : 1.0f;
}
