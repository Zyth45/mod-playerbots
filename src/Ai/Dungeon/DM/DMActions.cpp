/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DMActions.h"

#include <cmath>
#include <limits>

#include "AiObjectContext.h"
#include "Group.h"
#include "Playerbots.h"

using namespace DungeonBoss;
using namespace DireMaul;

namespace
{
    bool AwayFromGroup(Player* bot, float x, float y, float distance, bool (*counts)(Player*))
    {
        Group* group = bot->GetGroup();
        if (!group)
            return true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && member->IsAlive() && (!counts || counts(member)) &&
                    std::hypot(member->GetPositionX() - x, member->GetPositionY() - y) < distance)
                    return false;
        return true;
    }
}

bool DmLeaveMassiveGeyserAction::Execute(Event /*event*/)
{
    Unit* hydrospawn = Hydrospawn(botAI, context);
    if (!hydrospawn)
        return false;

    std::vector<Position> const centers = GeyserCenters(bot, hydrospawn);
    bool const target = IsGeyserTarget(bot, hydrospawn);
    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);

    std::vector<Position> const candidates = RingAround(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                                        { 6.0f, 10.0f, 14.0f, 18.0f }, 12);
    return MoveToReachableSpot(candidates, [this, &centers, target](float x, float y, float)
    {
        if (!SafeFromGeysers(centers, x, y))
            return false;
        return !target || AwayFromGroup(bot, x, y, GEYSER_SAFE, nullptr);
    });
}

bool DmGeyserSpreadAction::Execute(Event /*event*/)
{
    Unit* hydrospawn = Hydrospawn(botAI, context);
    if (!hydrospawn)
        return false;

    std::vector<Position> const centers = GeyserCenters(bot, hydrospawn);
    std::vector<Position> const candidates = RingAround(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                                        { 4.0f, 7.0f, 10.0f }, 12);
    return MoveToReachableSpot(candidates, [this, hydrospawn, &centers](float x, float y, float)
    {
        float const fromBoss = std::hypot(hydrospawn->GetPositionX() - x, hydrospawn->GetPositionY() - y);
        return fromBoss >= SPREAD_MIN_FROM_BOSS && fromBoss <= SPREAD_MAX_FROM_BOSS &&
               SafeFromGeysers(centers, x, y) && AwayFromGroup(bot, x, y, SPREAD_WANTED, &KeepsSpread);
    });
}

bool DmAttackHydrolingAction::Execute(Event /*event*/)
{
    Unit* nearest = nullptr;
    float nearestDistance = std::numeric_limits<float>::max();
    for (Unit* hydroling : Hydrolings(botAI, bot, context))
    {
        float const distance = bot->GetDistance(hydroling);
        if (distance < nearestDistance)
        {
            nearest = hydroling;
            nearestDistance = distance;
        }
    }
    if (!nearest || AI_VALUE(Unit*, "current target") == nearest)
        return false;
    return Attack(nearest);
}
