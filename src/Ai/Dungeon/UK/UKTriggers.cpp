/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UKTriggers.h"
#include "AiObjectContext.h"
#include "Map.h"
#include "Playerbots.h"

#include <cmath>

bool KelesethFrostTombTrigger::IsActive()
{
    GuidVector members = AI_VALUE(GuidVector, "group members");
    for (auto& member : members)
    {
        Unit* unit = botAI->GetUnit(member);
        if (unit && unit->HasAura(SPELL_FROST_TOMB))
        {
            return true;
        }
    }
    return false;
}

bool DalronnDpsTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "dalronn the controller");
    if (!boss || !boss->isTargetableForAttack()) { return false; }

    // This doesn't cause issues with healers currently and they will continue to heal even when included here
    return !botAI->IsTank(bot);
}

bool IngvarDreadfulRoarTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
    if (!boss) { return false; }

    if (boss->FindCurrentSpellBySpellId(SPELL_DREADFUL_ROAR))
    {
        return true;
    }
    return false;
}

bool IngvarSmashTankTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
    if (!boss || !botAI->IsTank(bot)) { return false; }

    if (boss->FindCurrentSpellBySpellId(SPELL_SMASH) ||
        boss->FindCurrentSpellBySpellId(SPELL_DARK_SMASH))
        {
            return true;
        }
    return false;
}

bool IngvarSmashTankReturnTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
    // if (!boss || !botAI->IsTank(bot) || boss->HasUnitState(UNIT_STATE_CASTING))
    // Ignore casting state as Ingvar will sometimes chain-cast a roar after a smash..
    // We don't want this to prevent our tank from repositioning properly.
    if (!boss || !botAI->IsTank(bot)) { return false; }

    return true;
}

namespace IngvarCover
{
    Unit* Boss(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
        return boss && boss->IsAlive() ? boss : nullptr;
    }

    bool CastingRoar(Unit* boss)
    {
        return boss && (DungeonBoss::IsCastingNamed(boss, "Staggering Roar") ||
                        DungeonBoss::IsCastingNamed(boss, "Dreadful Roar"));
    }

    bool Hidden(Player* bot, Unit* boss, float x, float y, float z)
    {
        float const chest = z + bot->GetCollisionHeight();
        float sx, sy, sz;
        boss->GetHitSpherePointFor({ x, y, chest }, sx, sy, sz);
        return !boss->GetMap()->isInLineOfSight(sx, sy, sz, x, y, chest, boss->GetPhaseMask(),
                                                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2);
    }

    bool NearCover(Player* bot, Unit* boss, float x, float y, float z)
    {
        if (Hidden(bot, boss, x, y, z))
            return false;
        for (uint32 i = 0; i < 8; ++i)
        {
            float const angle = float(M_PI) * i / 4.0f;
            float const nx = x + std::cos(angle) * READY_STEP;
            float const ny = y + std::sin(angle) * READY_STEP;
            float nz = z;
            bot->UpdateAllowedPositionZ(nx, ny, nz);
            if (std::fabs(nz - z) < 2.0f && Hidden(bot, boss, nx, ny, nz))
                return true;
        }
        return false;
    }

    Position TankSpot(Unit* boss)
    {
        Creature* creature = boss->ToCreature();
        Position const home = creature ? creature->GetHomePosition() : boss->GetPosition();
        float const away = home.GetOrientation() + float(M_PI);
        return Position(home.GetPositionX() + std::cos(away) * TANK_OFFSET,
                        home.GetPositionY() + std::sin(away) * TANK_OFFSET, home.GetPositionZ());
    }
}

bool IngvarRoarTrigger::IsActive()
{
    Unit* boss = IngvarCover::Boss(botAI, context);
    return boss && IngvarCover::CastingRoar(boss) &&
           !IngvarCover::Hidden(bot, boss, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
}

bool IngvarCoverReadyTrigger::IsActive()
{
    if (botAI->IsTank(bot) || !(botAI->IsRanged(bot) || botAI->IsHeal(bot)))
        return false;
    Unit* boss = IngvarCover::Boss(botAI, context);
    if (!boss || !boss->IsInCombat() || IngvarCover::CastingRoar(boss))
        return false;
    return !IngvarCover::NearCover(bot, boss, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
}

bool IngvarTankSpotTrigger::IsActive()
{
    if (!botAI->IsMainTank(bot))
        return false;
    Unit* boss = IngvarCover::Boss(botAI, context);
    if (!boss || !boss->IsInCombat() || boss->HasUnitState(UNIT_STATE_CASTING))
        return false;
    Position const spot = IngvarCover::TankSpot(boss);
    return bot->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) > IngvarCover::TANK_TOLERANCE;
}

bool NotBehindIngvarTrigger::IsActive()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "ingvar the plunderer");
    if (!boss || botAI->IsTank(bot)) { return false; }

    return AI_VALUE2(bool, "behind", "current target");
}
