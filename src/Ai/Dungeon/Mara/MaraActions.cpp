/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MaraActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace Maraudon;

bool MaraShareBoulderAction::Execute(Event /*event*/)
{
    Unit* landslide = nullptr;
    Unit* target = BoulderToShare(botAI, context, bot, landslide);
    if (!target)
        return false;

    // Behind the target as seen from Landslide, out of his Trample around himself as far as the boulder allows.
    float const away = landslide->GetAngle(target);
    float const x = target->GetPositionX() + std::cos(away) * BOULDER_GATHER_BEHIND;
    float const y = target->GetPositionY() + std::sin(away) * BOULDER_GATHER_BEHIND;
    return MoveTo(bot->GetMapId(), x, y, target->GetPositionZ(), false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool MaraMoveFromFrontalConeAction::Execute(Event /*event*/)
{
    FrontalCone const* cone = nullptr;
    Unit* boss = ConeOver(botAI, context, bot, FRONTAL_CONES, cone);
    return boss && MoveOutOfCone(boss, *cone);
}

bool MaraMoveFromDustFieldAction::Execute(Event /*event*/)
{
    Unit* theradras = TheradrasDusting(botAI, context);
    return theradras && KeepAway(theradras, DUST_FIELD_RADIUS);
}
