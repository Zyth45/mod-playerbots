/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Actions.h"
#include "Aq40Helpers.h"
#include "ObjectMgr.h"
#include "Playerbots.h"
#include "Timer.h"

using namespace Aq40Helpers;

namespace
{
// Standing on the exit pad, the area trigger is sent again this often while
// the server's delayed checks run.
constexpr uint32 STOMACH_EXIT_RETRY_MS = 4000;
constexpr uint32 SPIT_OUT_RETRY_MS = 3000;
constexpr float STOMACH_EXIT_ARRIVAL = 2.0f;

// Server bots have no client to fire an area trigger.
void SendAreaTrigger(Player* bot, uint32 triggerId)
{
    WorldPacket packet(CMSG_AREATRIGGER);
    packet << triggerId;
    packet.rpos(0);
    bot->GetSession()->HandleAreaTriggerOpcode(packet);
}
}  // namespace

bool Aq40CthunLeaveStomachAction::Execute(Event /*event*/)
{
    AreaTrigger const* exit = sObjectMgr->GetAreaTrigger(AT_CTHUN_STOMACH_EXIT);
    if (!exit)
        return false;

    if (bot->GetExactDist2d(exit->x, exit->y) > STOMACH_EXIT_ARRIVAL)
    {
        bot->AttackStop();
        bot->CastStop();
        return MoveTo(bot->GetMapId(), exit->x, exit->y, exit->z, false, false, false, false,
                      MovementPriority::MOVEMENT_FORCED);
    }

    if (IsSelfBot(bot))
        return false;

    // Stay on the pad: the trigger throws the player up after a short delay.
    if (bot->isMoving())
        bot->StopMoving();
    uint32 const now = getMSTime();
    if (_lastExitAttemptMs && getMSTimeDiff(_lastExitAttemptMs, now) < STOMACH_EXIT_RETRY_MS)
        return true;
    _lastExitAttemptMs = now;
    SendAreaTrigger(bot, AT_CTHUN_STOMACH_EXIT);
    return true;
}

bool Aq40CthunDodgeGlareAction::Execute(Event /*event*/)
{
    Creature* eye = GetEye(bot);
    if (!eye)
        return false;

    Position const goal = GetGlareDodge(bot, eye);
    bot->CastStop();
    return MoveTo(bot->GetMapId(), goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ(), false, false, false,
                  false, MovementPriority::MOVEMENT_FORCED);
}

bool Aq40CthunLookAwayAction::Execute(Event /*event*/)
{
    Unit* caster = GetEldritchCaster(bot);
    if (!caster)
        return false;

    // Casting or attacking turns the bot back toward its target, so both stop
    // until the last pulse; the multiplier holds them back meanwhile.
    bot->AttackStop();
    bot->CastStop();
    if (bot->isMoving())
        bot->StopMoving();
    bot->SetFacingTo(caster->GetAngle(bot));
    return true;
}

bool Aq40CthunAvoidDevourAction::Execute(Event /*event*/)
{
    Player* prey = GetDevourPreyNear(bot);
    if (!prey)
        return false;

    // Whoever stands within 8 yd of the prey when the mouth reaches it is swallowed too.
    return FleePosition(prey->GetPosition(), DEVOUR_CLEAR_DISTANCE, 500, MovementPriority::MOVEMENT_FORCED);
}

bool Aq40CthunSpitOutAction::Execute(Event /*event*/)
{
    if (IsSelfBot(bot))
        return false;

    uint32 const now = getMSTime();
    if (_lastAttemptMs && getMSTimeDiff(_lastAttemptMs, now) < SPIT_OUT_RETRY_MS)
        return false;
    _lastAttemptMs = now;
    SendAreaTrigger(bot, AT_CTHUN_CENTER);
    return true;
}

bool Aq40CthunMoveToSpotAction::Execute(Event /*event*/)
{
    Position spot;
    if (!GetCthunSpot(bot, spot))
        return false;

    // Keep the height the bot is at: the spots are all on the room floor.
    float z = bot->GetPositionZ();
    bot->UpdateAllowedPositionZ(spot.GetPositionX(), spot.GetPositionY(), z);
    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), z, false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}

bool Aq40CthunAttackAction::Execute(Event /*event*/)
{
    Unit* target = ChooseCthunTarget(bot);
    if (!target || target == AI_VALUE(Unit*, "current target"))
        return false;
    return Attack(target);
}
