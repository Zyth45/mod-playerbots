/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DMMultipliers.h"

#include "AiObjectContext.h"
#include "DMTriggers.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace DireMaul;

float HydrospawnMultiplier::GetValue(Action* action)
{
    if (!MovesIntoDanger(action))
        return 1.0f;

    // While a geyser stands or is being cast, chasing, following and the formation would walk a bot back into it.
    Unit* hydrospawn = Hydrospawn(botAI, context);
    if (!hydrospawn)
        return 1.0f;
    std::vector<Position> const centers = GeyserCenters(bot, hydrospawn);
    return centers.empty() || IsGeyserTarget(bot, hydrospawn) ? 1.0f : 0.0f;
}
