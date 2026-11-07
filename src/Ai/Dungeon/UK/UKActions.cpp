/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UKActions.h"
#include "Playerbots.h"

bool AttackFrostTombAction::isUseful() { return !botAI->IsHeal(bot); }
bool AttackFrostTombAction::Execute(Event /*event*/)
{
    Unit* frostTomb = nullptr;

    // Target is not findable from threat table using AI_VALUE2(),
    // therefore need to search manually for the unit name
    GuidVector targets = AI_VALUE(GuidVector, "possible targets no los");

    for (auto& target : targets)
    {
        Unit* unit = botAI->GetUnit(target);
        if (unit && unit->GetEntry() == NPC_FROST_TOMB)
        {
            frostTomb = unit;
            break;
        }
    }
    if (!frostTomb || AI_VALUE(Unit*, "current target") == frostTomb)
    {
        return false;
    }
    return Attack(frostTomb);
}

// TODO: Possibly add player stacking behaviour close to tank, to prevent Skarvald charging ranged
bool AttackDalronnAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "dalronn the controller");
    if (!boss) { return false; }

    if (AI_VALUE(Unit*, "current target") == boss)
    {
        return false;
    }
    return Attack(boss);
}

bool IngvarDodgeSmashAction::isUseful() { return !AI_VALUE2(bool, "behind", "current target"); }
bool IngvarDodgeSmashAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
    if (!boss) { return false; }

    float distance = bot->GetExactDist2d(boss->GetPosition());
    // Extra units to move into the boss, instead of being just 1 pixel past his midpoint.
    // Can be adjusted - this value tends to mirror how a human would play,
    // and visibly ensures you won't get hit while not creating excessive movements.
    float distanceExtra = 2.0f;
    return Move(bot->GetAngle(boss), distance + distanceExtra);
}

bool IngvarSmashReturnAction::isUseful() { return AI_VALUE2(bool, "behind", "current target"); }
bool IngvarSmashReturnAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
    if (!boss) { return false; }

    float distance = bot->GetExactDist2d(boss->GetPosition());
    return Move(bot->GetAngle(boss), distance + bot->GetMeleeReach());
}

bool IngvarHideFromRoarAction::Execute(Event /*event*/)
{
    Unit* boss = IngvarCover::Boss(botAI, context);
    if (!boss || !IngvarCover::CastingRoar(boss))
        return false;

    std::vector<Position> const candidates = DungeonBoss::RingAround(bot->GetPositionX(), bot->GetPositionY(),
                                                                     bot->GetPositionZ(), { 3.0f, 6.0f, 9.0f, 12.0f }, 16);
    return MoveToReachableSpot(candidates, [this, boss](float x, float y, float z)
        { return IngvarCover::Hidden(bot, boss, x, y, z); }, IngvarCover::HIDE_MAX_PATH);
}

bool IngvarCoverReadyAction::Execute(Event /*event*/)
{
    Unit* boss = IngvarCover::Boss(botAI, context);
    if (!boss)
        return false;

    std::vector<Position> const candidates = DungeonBoss::RingAround(boss->GetPositionX(), boss->GetPositionY(),
                                                                     boss->GetPositionZ(), { 8.0f, 11.0f, 14.0f, 18.0f }, 16);
    return MoveToReachableSpot(candidates, [this, boss](float x, float y, float z)
        {
            return boss->GetExactDist2d(x, y) <= IngvarCover::READY_MAX_FROM_BOSS &&
                   IngvarCover::NearCover(bot, boss, x, y, z);
        }, IngvarCover::READY_MAX_PATH);
}

bool IngvarHoldSpotAction::Execute(Event /*event*/)
{
    Unit* boss = IngvarCover::Boss(botAI, context);
    if (!boss)
        return false;
    Position const spot = IngvarCover::TankSpot(boss);
    if (bot->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) <= IngvarCover::TANK_TOLERANCE)
        return false;
    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false, false,
                  true, MovementPriority::MOVEMENT_COMBAT);
}
