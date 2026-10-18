/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UldaTriggers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace Uldaman
{
    Unit* ArchaedasNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot)
    {
        Unit* archaedas = AI_VALUE2(Unit*, "find target", "archaedas");
        return archaedas && archaedas->IsAlive() && Within(bot, archaedas, GROUND_TREMOR_RADIUS) ? archaedas
                                                                                                 : nullptr;
    }
}

using namespace Uldaman;

bool UldaGroundTremorTrigger::IsActive()
{
    return !botAI->IsTank(bot) && botAI->IsRanged(bot) && ArchaedasNear(botAI, context, bot);
}
