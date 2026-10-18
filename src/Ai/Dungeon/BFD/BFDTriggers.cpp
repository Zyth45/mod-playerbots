/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BFDTriggers.h"

#include "AiObjectContext.h"
#include "Helpers.h"
#include "Playerbots.h"

#include <cmath>

using namespace DungeonBoss;

namespace BlackfathomDeeps
{
    Unit* FindGhamoora(PlayerbotAI* botAI, AiObjectContext* context)
    {
        if (Unit* ghamoora = AI_VALUE2(Unit*, "find target", "ghamoo-ra"))
            return ghamoora;

        std::wstring const key = CoaNameKey("ghamoo-ra");
        for (ObjectGuid const& guid : AI_VALUE(GuidVector, "possible targets no los"))
            if (Unit* unit = botAI->GetUnit(guid))
                if (unit->IsAlive() && CoaNameIs(unit->GetName(), key))
                    return unit;
        return nullptr;
    }

    bool IsTorrenting(Unit const* ghamoora)
    {
        return ghamoora && ghamoora->IsAlive() &&
               (HasAuraNamed(ghamoora, "Torrent") || IsCastingNamed(ghamoora, "Torrent"));
    }

    bool OffHisBack(Unit const* ghamoora, Player* bot)
    {
        if (bot->GetExactDist2d(ghamoora) > TORRENT_REACH + ghamoora->GetCombatReach())
            return false;

        return std::fabs(RelativeAngle(ghamoora, bot)) < float(M_PI) - TORRENT_BACK_TOLERANCE;
    }
}

using namespace BlackfathomDeeps;

bool BfdGhamooraTorrentTrigger::IsActive()
{
    Unit* ghamoora = FindGhamoora(botAI, context);
    return IsTorrenting(ghamoora) && OffHisBack(ghamoora, bot);
}

bool BfdFrontalConeTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    FrontalCone const* cone = nullptr;
    return ConeOver(botAI, context, bot, FRONTAL_CONES, cone) != nullptr;
}
