/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OtherSideActions.h"
#include "OtherSideShared.h"
#include "Playerbots.h"

using namespace OtherSideShared;

bool OtherSideLeaveHazardAction::Execute(Event /*event*/)
{
    Hazards const hazards = CollectHazards(bot);
    if (!hazards.Contains(bot->GetPositionX(), bot->GetPositionY()))
        return false;

    Position spot;
    Position center;
    Position ahead;
    bool const eggPhase = EggPhaseCenter(bot, center);
    if (eggPhase)
        ahead = NextRunSpot(bot, center);
    if (!FindSafeSpot(bot, hazards, spot, eggPhase ? &ahead : nullptr))
        return false;

    bot->CastStop();
    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                  true, MovementPriority::MOVEMENT_COMBAT, true);
}

// An egg the group steps on is kicked away and never hatches.
bool OtherSideKickEggAction::Execute(Event /*event*/)
{
    Creature* egg = EggToKick(bot);
    if (!egg)
        return false;

    return MoveTo(bot->GetMapId(), egg->GetPositionX(), egg->GetPositionY(), egg->GetPositionZ(), false, false, false,
                  true, MovementPriority::MOVEMENT_COMBAT, true);
}

// A marked player who stands next to Muzah stops his Cobra Shots.
bool OtherSideCloseInOnMuzahAction::Execute(Event /*event*/)
{
    Unit* muzah = bot->FindNearestCreature(NPC_MUZAH, 80.0f);
    if (!muzah)
        return false;
    return MoveTo(muzah, muzah->GetCombatReach(), MovementPriority::MOVEMENT_COMBAT);
}

// The spiders the eggs hatch go for the tank; damage dealers take them down first.
bool OtherSideAttackSpiderAction::Execute(Event /*event*/)
{
    Unit* spider = NearestSpider(bot);
    if (!spider || AI_VALUE(Unit*, "current target") == spider)
        return false;
    return Attack(spider);
}

bool OtherSideRunAroundAction::Execute(Event /*event*/)
{
    Position center;
    if (!EggPhaseCenter(bot, center))
        return false;

    Position const spot = NextRunSpot(bot, center);
    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                  true, MovementPriority::MOVEMENT_COMBAT, true);
}
