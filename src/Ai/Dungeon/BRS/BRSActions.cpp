/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRSActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace BlackrockSpire;
using namespace DungeonBoss;

bool BrsMoveFromLineAttackAction::Execute(Event /*event*/)
{
    float angle = 0.0f;
    float halfWidth = 0.0f;
    Unit* boss = LineOver(botAI, context, bot, angle, halfWidth);
    return boss && MoveOutOfPath(boss, angle, halfWidth, LINE_SIDE_STEP);
}

bool BrsMoveFromBladestormAction::Execute(Event /*event*/)
{
    Unit* rend = RendStorming(botAI, context);
    return rend && KeepAway(rend, BLADESTORM_RADIUS);
}

bool BrsMoveFromFrontalConeAction::Execute(Event /*event*/)
{
    FrontalCone const* cone = nullptr;
    Unit* boss = ConeOver(botAI, context, bot, FRONTAL_CONES, cone);
    return boss && MoveOutOfCone(boss, *cone);
}
