/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFCMultipliers.h"
#include "AiObjectContext.h"
#include "ChooseTargetActions.h"
#include "GenericSpellActions.h"
#include "Playerbots.h"
#include "RFCActions.h"
#include "RFCTriggers.h"

namespace
{
    // Without this the ordinary "attack what the tank attacks" behaviour drags every bot straight
    // back onto the boss the moment the priority action has switched it away.
    bool IsAssist(Action* action) { return dynamic_cast<DpsAssistAction*>(action) != nullptr; }
}

float OggleflintMultiplier::GetValue(Action* action)
{
    if (!IsAssist(action))
        return 1.0f;

    return RagefireEscort::Nearest(botAI, bot, context, "oggleflint",
                                   {NPC_RAGEFIRE_SHAMAN, NPC_RAGEFIRE_TROGG}) ? 0.0f : 1.0f;
}

float JergoshTheInvokerMultiplier::GetValue(Action* action)
{
    if (!IsAssist(action))
        return 1.0f;

    return RagefireEscort::Nearest(botAI, bot, context, "jergosh the invoker",
                                   {NPC_SEARING_BLADE_CULTIST}) ? 0.0f : 1.0f;
}

float BazzalanMultiplier::GetValue(Action* action)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "bazzalan");
    if (!boss || !boss->isTargetableForAttack())
        return 1.0f;

    return IsAssist(action) ? 0.0f : 1.0f;
}

float TaragamanTheHungererMultiplier::GetValue(Action* action)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "taragaman the hungerer");
    if (!boss)
        return 1.0f;

    // Behind the pillar the bot has no line of sight to the boss, and every movement or chase
    // action would walk it back out into the nova.
    if (TaragamanPillar::ShouldHide(botAI, bot, context))
    {
        if (dynamic_cast<TaragamanHideBehindPillarAction*>(action))
            return 1.0f;

        return dynamic_cast<MovementAction*>(action) || IsAssist(action) ? 0.0f : 1.0f;
    }

    if (!botAI->IsRanged(bot))
        return 1.0f;

    // While a ranged bot is still inside the nova, nothing else may move it: the formation and
    // follow actions would walk it straight back in.
    if (bot->GetExactDist2d(boss->GetPosition()) < FIRE_NOVA_RADIUS + FIRE_NOVA_SAFETY)
    {
        if (dynamic_cast<MovementAction*>(action) && !dynamic_cast<TaragamanAvoidFireNovaAction*>(action))
            return 0.0f;
    }

    return 1.0f;
}
