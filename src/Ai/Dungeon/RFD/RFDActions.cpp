/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFDActions.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace RazorfenDowns;

bool RfdMoveFromExpelBloatAction::Execute(Event /*event*/)
{
    Unit* glutton = GluttonExpelling(botAI, context);
    return glutton && KeepAway(glutton, EXPEL_BLOAT_RADIUS);
}

bool RfdMoveFromBoneStormAction::Execute(Event /*event*/)
{
    Unit* mordresh = MordreshStorming(botAI, context);
    return mordresh && KeepAway(mordresh, BONE_STORM_RADIUS);
}

bool RfdMoveFromMarkedAction::Execute(Event /*event*/)
{
    float radius = 0.0f;
    Player* marked = MarkedNear(bot, radius);
    return marked && KeepAway(marked, radius);
}
