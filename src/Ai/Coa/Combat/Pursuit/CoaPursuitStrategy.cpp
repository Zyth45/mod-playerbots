/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaPursuitStrategy.h"

#include "CoaGroupTank.h"
#include "CoaPursuitRule.h"
#include "MotionMaster.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"

namespace
{
    bool IsFleeing(Unit* target)
    {
        if (!target || target->GetTypeId() != TYPEID_UNIT)
            return false;
        switch (target->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            case FLEEING_MOTION_TYPE:
            case ASSISTANCE_MOTION_TYPE:
            case TIMED_FLEEING_MOTION_TYPE:
                return true;
            default:
                return false;
        }
    }
}

float CoaPursuitMultiplier::GetValue(Action* action)
{
    if (!action || !sPlayerbotAIConfig.coaCombatPursuitStopEnable)
        return 1.0f;

    // Only a chase: stock reach / chase, or a gap-closer (Charge, Intercept, Feral Charge).
    std::string targetName;
    float range = 0.0f;
    if (auto* reach = dynamic_cast<ReachTargetAction*>(action))
    {
        targetName = reach->GetTargetName();
        if (action->getName() == "reach spell")
            range = botAI->GetRange("spell");
    }
    else if (auto* leap = dynamic_cast<CastReachTargetSpellAction*>(action))
        targetName = leap->GetTargetName();
    else
        return 1.0f;

    if (!CoaGroupTank::GuardScope(bot) || PlayerbotAI::IsTank(bot))
        return 1.0f;

    Unit* target = context->GetValue<Unit*>(targetName)->Get();
    if (!IsFleeing(target) || target->FindMap() != bot->FindMap())
        return 1.0f;

    Player* tank = CoaGroupTank::Reference(bot);
    CoaPursuitRule::Situation s;
    s.chaseAction = true;
    s.targetFleeing = true;
    s.hasTank = tank && tank != bot && tank->IsAlive();
    s.botIsTank = false;
    if (s.hasTank)
        s.chaseEndToTank = CoaPursuitRule::ChaseEndToTank(bot->GetPositionX(), bot->GetPositionY(),
                                                          target->GetPositionX(), target->GetPositionY(),
                                                          tank->GetPositionX(), tank->GetPositionY(), range);

    return CoaPursuitRule::Forbids(s) ? 0.0f : 1.0f;
}

void CoaPursuitStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaPursuitMultiplier(botAI));
}
