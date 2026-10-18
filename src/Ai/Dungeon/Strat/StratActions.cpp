/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StratActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace Stratholme;

bool StratMoveFromCrusadersHammerAction::Execute(Event /*event*/)
{
    Unit* dathrohan = DathrohanNear(botAI, context, bot);
    return dathrohan && KeepAway(dathrohan, CRUSADERS_HAMMER_RADIUS);
}

bool StratMoveFromFrontalConeAction::Execute(Event /*event*/)
{
    FrontalCone const* cone = nullptr;
    Unit* boss = ConeOver(botAI, context, bot, FRONTAL_CONES, cone);
    return boss && MoveOutOfCone(boss, *cone);
}

bool StratMoveFromRetributionAction::Execute(Event /*event*/)
{
    Player* marked = RetributionNear(botAI, context, bot);
    return marked && KeepAway(marked, DIVINE_RETRIBUTION_RADIUS);
}
