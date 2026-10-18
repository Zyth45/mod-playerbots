/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRDActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace BlackrockDepths;
using namespace DungeonBoss;

bool BrdMoveFromGoutAction::Execute(Event /*event*/)
{
    Creature* guardian = GoutOver(bot);
    if (!guardian)
        return false;

    FrontalCone const gout{ nullptr, GOUT_OF_FLAME_REACH, GOUT_OF_FLAME_HALF_ARC, "Gout of Flame" };
    return MoveOutOfCone(guardian, gout);
}

bool BrdAttackBurningSpiritAction::Execute(Event /*event*/)
{
    Creature* spirit = BurningSpiritToKill(botAI, context, bot);
    if (!spirit || AI_VALUE(Unit*, "current target") == spirit)
        return false;

    return Attack(spirit);
}

bool BrdAttackThaurissanAction::Execute(Event /*event*/)
{
    Unit* thaurissan = ThaurissanToFocus(botAI, context);
    if (!thaurissan || AI_VALUE(Unit*, "current target") == thaurissan)
        return false;

    return Attack(thaurissan);
}

bool BrdMoveFromGroundTremorAction::Execute(Event /*event*/)
{
    Unit* boss = GroundTremorNear(botAI, context, bot);
    return boss && KeepAway(boss, GROUND_TREMOR_RADIUS);
}

bool BrdMoveFromFrontalConeAction::Execute(Event /*event*/)
{
    FrontalCone const* cone = nullptr;
    Unit* boss = ConeOver(botAI, context, bot, FRONTAL_CONES, cone);
    return boss && MoveOutOfCone(boss, *cone);
}
