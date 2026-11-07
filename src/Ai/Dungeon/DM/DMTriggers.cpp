/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DMTriggers.h"

#include <algorithm>
#include <cmath>
#include <list>

#include "AiObjectContext.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace DireMaul
{
    Unit* Hydrospawn(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", "hydrospawn");
        return boss && boss->IsAlive() ? boss : nullptr;
    }

    std::vector<Position> GeyserCenters(Player* bot, Unit* hydrospawn)
    {
        std::vector<Position> centers;
        std::list<Creature*> geysers;
        bot->GetCreatureListWithEntryInGrid(geysers, NPC_MASSIVE_GEYSER, GEYSER_SEARCH);
        for (Creature* geyser : geysers)
            if (geyser->IsAlive())
                centers.push_back(geyser->GetPosition());
        if (Unit* target = hydrospawn ? CastTarget(hydrospawn, "Massive Geyser") : nullptr)
            if (target != bot)
                centers.push_back(target->GetPosition());
        return centers;
    }

    bool IsGeyserTarget(Player* bot, Unit* hydrospawn)
    {
        return hydrospawn && CastTarget(hydrospawn, "Massive Geyser") == bot;
    }

    bool SafeFromGeysers(std::vector<Position> const& centers, float x, float y)
    {
        return std::none_of(centers.begin(), centers.end(), [x, y](Position const& center)
            { return std::hypot(center.GetPositionX() - x, center.GetPositionY() - y) < GEYSER_SAFE; });
    }

    bool KeepsSpread(Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && (PlayerbotAI::IsRanged(bot) || PlayerbotAI::IsHeal(bot));
    }

    std::vector<Unit*> Hydrolings(PlayerbotAI* botAI, Player* bot, AiObjectContext* context)
    {
        std::vector<Unit*> hydrolings;
        for (ObjectGuid const guid : AI_VALUE(GuidVector, "possible targets"))
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && unit->GetEntry() == NPC_HYDROLING && unit->IsAlive() && unit->IsInCombat() &&
                bot->IsWithinDistInMap(unit, HYDROLING_SEARCH))
                hydrolings.push_back(unit);
        }
        return hydrolings;
    }
}

using namespace DireMaul;

bool DmMassiveGeyserTrigger::IsActive()
{
    Unit* hydrospawn = Hydrospawn(botAI, context);
    if (!hydrospawn)
        return false;

    if (IsGeyserTarget(bot, hydrospawn))
    {
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member != bot && member->IsAlive() && bot->GetExactDist2d(member) < GEYSER_SAFE)
                        return true;
        return false;
    }

    return !SafeFromGeysers(GeyserCenters(bot, hydrospawn), bot->GetPositionX(), bot->GetPositionY());
}

bool DmGeyserSpreadTrigger::IsActive()
{
    Unit* hydrospawn = Hydrospawn(botAI, context);
    if (!hydrospawn || !hydrospawn->IsInCombat() || !KeepsSpread(bot))
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member != bot && member->IsAlive() && KeepsSpread(member) &&
                bot->GetExactDist2d(member) < SPREAD_TOO_CLOSE)
                return true;
    return false;
}

bool DmHydrolingsTrigger::IsActive()
{
    if (!Hydrospawn(botAI, context) || botAI->IsTank(bot) || botAI->IsHeal(bot))
        return false;

    std::vector<Unit*> const hydrolings = Hydrolings(botAI, bot, context);
    if (hydrolings.size() < HYDROLING_SWITCH)
        return false;
    Unit* current = AI_VALUE(Unit*, "current target");
    return !current || current->GetEntry() != NPC_HYDROLING;
}
