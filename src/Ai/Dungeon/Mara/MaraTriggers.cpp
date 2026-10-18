/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MaraTriggers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace Maraudon
{
    Unit* BoulderToShare(PlayerbotAI* botAI, AiObjectContext* context, Player* bot, Unit*& landslide)
    {
        landslide = AI_VALUE2(Unit*, "find target", "landslide");
        if (!landslide || !landslide->IsAlive())
            return nullptr;

        Unit* target = CastTarget(landslide, "Hurl Boulder");
        if (!target || target == bot)
            return nullptr;

        float const distance = bot->GetExactDist2d(target);
        return distance > BOULDER_GATHER_DISTANCE && distance < BOULDER_GATHER_RANGE ? target : nullptr;
    }

    Unit* TheradrasDusting(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* theradras = AI_VALUE2(Unit*, "find target", "princess theradras");
        return theradras && theradras->IsAlive() && HasAuraNamed(theradras, "Dust Field") ? theradras : nullptr;
    }
}

using namespace Maraudon;

bool MaraHurlBoulderTrigger::IsActive()
{
    Unit* landslide = nullptr;
    return BoulderToShare(botAI, context, bot, landslide) != nullptr;
}

bool MaraFrontalConeTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    FrontalCone const* cone = nullptr;
    return ConeOver(botAI, context, bot, FRONTAL_CONES, cone) != nullptr;
}

bool MaraDustFieldTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    Unit* theradras = TheradrasDusting(botAI, context);
    return theradras && Within(bot, theradras, DUST_FIELD_RADIUS);
}
