/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ZGActions.h"

#include <algorithm>

#include "Playerbots.h"
#include "ZGHelpers.h"

using namespace ZulGurubHelpers;

bool BloodlordMandokirStopCausingThreatAction::Execute(Event /*event*/)
{
    // Auto-attack, Auto Shot and a cast under way; the multiplier blocks new ones.
    bot->AttackStop();
    bot->InterruptNonMeleeSpells(false);
    return true;
}

bool HakkarAvoidBloodScytheAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "hakkar");
    if (!boss)
        return false;

    float const distance = std::max(bot->GetDistance2d(boss), bot->GetMeleeRange(boss) * 0.5f);
    Position left = boss->GetPosition();
    Position right = boss->GetPosition();
    left.RelocatePolarOffset(HAKKAR_SCYTHE_FLANK_ANGLE, distance);
    right.RelocatePolarOffset(-HAKKAR_SCYTHE_FLANK_ANGLE, distance);
    Position const& destination = bot->GetExactDist2d(left) < bot->GetExactDist2d(right) ? left : right;

    float x = destination.GetPositionX();
    float y = destination.GetPositionY();
    float z = boss->GetPositionZ();
    bool const exact = bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                                     bot->GetPositionZ(), x, y, z);

    return MoveTo(boss->GetMapId(), x, y, z, false, false, false, exact, MovementPriority::MOVEMENT_COMBAT);
}
