/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SMMultipliers.h"

#include "AiObjectContext.h"
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "SMActions.h"
#include "SMTriggers.h"

using namespace ScarletMonastery;

namespace
{
    // Anything that walks a bot back to the boss: attacking, melee, moving into spell range, following.
    bool ApproachesBoss(Action* action)
    {
        return dynamic_cast<AttackAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action) ||
               dynamic_cast<MovementAction*>(action);
    }
}

float ArcanistDoanMultiplier::GetValue(Action* action)
{
    if (dynamic_cast<SmMoveFromDoanAction*>(action))
        return 1.0f;

    // Inside Arcane Bubble he takes no damage, so nobody, the tank included, has a reason to stay in Detonation.
    Unit* doan = AI_VALUE2(Unit*, "find target", "arcanist doan");
    return IsDetonating(doan) && ApproachesBoss(action) ? 0.0f : 1.0f;
}

float HerodMultiplier::GetValue(Action* action)
{
    if (botAI->IsTank(bot) || dynamic_cast<SmMoveFromHerodAction*>(action))
        return 1.0f;

    Unit* herod = AI_VALUE2(Unit*, "find target", "herod");
    return IsWhirlwinding(herod) && ApproachesBoss(action) ? 0.0f : 1.0f;
}

float WhitemaneMultiplier::GetValue(Action* action)
{
    // The ordinary "attack what the tank attacks" would pull everyone back onto Mograine.
    if (!dynamic_cast<DpsAssistAction*>(action) || botAI->IsTank(bot) || botAI->IsHeal(bot))
        return 1.0f;

    return WhitemaneToFocus(botAI, context) ? 0.0f : 1.0f;
}
