/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBreathGuard.h"

#include <unordered_map>

#include "CoaBreathRule.h"
#include "CoaGroupTank.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Position.h"

namespace
{
    // World thread only.
    std::unordered_map<ObjectGuid, uint32> UnderWaterMs;      // per bot
    std::unordered_map<ObjectGuid, Position> LastDrySpot;     // per reference tank

    bool GuardActs(Player* bot)
    {
        if (!CoaGroupTank::GuardScope(bot))
            return false;
        return CoaGroupTank::InDungeonClearRun(bot) || sPlayerbotAIConfig.coaCombatBreathGuardEnable;
    }
}

bool CoaBreathGuard::AnyMemberUnderWater(Player* bot)
{
    if (!bot)
        return false;
    if (bot->IsUnderWater())
        return true;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member != bot && member->IsInWorld() && member->IsAlive() &&
            member->GetMapId() == bot->GetMapId() && member->IsUnderWater())
            return true;
    }
    return false;
}

void CoaBreathGuard::Tick(uint32 diffMs)
{
    for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !GET_PLAYERBOT_AI(bot) || !GuardActs(bot))
        {
            UnderWaterMs.erase(guid);
            continue;
        }

        Player* tank = CoaGroupTank::Reference(bot);
        if (tank && !tank->IsUnderWater())
            LastDrySpot[tank->GetGUID()] = tank->GetPosition();

        uint32& ms = UnderWaterMs[guid];
        if (!CoaBreathRule::Step(ms, diffMs, bot->IsUnderWater(), bot->IsInCombat()))
            continue;

        Position dest;
        if (tank && tank != bot && !tank->IsUnderWater())
            dest = tank->GetPosition();
        else if (auto itr = tank ? LastDrySpot.find(tank->GetGUID()) : LastDrySpot.end(); itr != LastDrySpot.end())
            dest = itr->second;
        else
            continue;  // no dry spot known yet: try again after the next limit

        bot->GetMotionMaster()->Clear();
        bot->NearTeleportTo(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), bot->GetOrientation());
        LOG_DEBUG("playerbots", "[coa combat:{}] breath guard: {}s under water out of combat, brought up",
                  bot->GetName(), CoaBreathRule::UNDER_WATER_LIMIT_MS / 1000);
    }
}
