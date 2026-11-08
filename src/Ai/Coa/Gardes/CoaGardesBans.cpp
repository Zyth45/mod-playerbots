/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaGardesCombat.h"

#include "Brd/CoaLyceumCloseUpDecision.h"
#include "Common/CoaGardesBotState.h"
#include "Common/CoaGardesGate.h"
#include "Common/CoaGardesRunHooks.h"
#include "Common/CoaGardesSettings.h"
#include "Common/CoaGardesUnits.h"
#include "RoomBoss/CoaRoomBossChase.h"

#include "Creature.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "Timer.h"


// The multipliers of the party combat guards (from Dungeon Clear's DungeonClear{,Combat}Multiplier) and the two
// strategies that carry every guard for every bot.
namespace
{

    // The Lyceum close-up, chase half: no stock reach / chase toward a target further than LEASH from the tank.
    bool LyceumChaseLeavesThePack(Player* bot, AiObjectContext* context, Action* action)
    {
        if (bot->GetMapId() != CoaBrd::MAP_ID)
            return false;
        auto* reach = dynamic_cast<ReachTargetAction*>(action);
        if (!reach || !CoaLyceumCloseUp::InRoom(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
            return false;
        if (!CoaGroupTank::ModuleGuardActs(bot, CoaGardesGate::LYCEUM_CLOSE_UP))
            return false;
        Player* tank = CoaGardesUnits::Tank(bot);
        CoaLyceumCloseUp::Gate g;
        g.hasTank = tank != nullptr;
        g.tankIsSelf = tank == bot;
        g.tankAlive = tank && tank->IsAlive();
        g.sameInstance = tank && tank->FindMap() == bot->FindMap();
        g.tankIsHuman = CoaGardesUnits::IsHuman(tank);
        g.botIsTank = PlayerbotAI::IsTank(bot);
        g.botInRoom = true;
        g.tankInRoom = tank && CoaLyceumCloseUp::InRoom(tank->GetPositionX(), tank->GetPositionY(), tank->GetPositionZ());
        if (!CoaLyceumCloseUp::Applies(g))
            return false;
        Unit* target = context->GetValue<Unit*>(reach->GetTargetName())->Get();
        return target && target->FindMap() == bot->FindMap() &&
               CoaLyceumCloseUp::ChaseLeavesThePack(target->GetPositionX(), target->GetPositionY(),
                                                    tank->GetPositionX(), tank->GetPositionY());
    }

    // The flagged room-aggro bosses of the bot's map within 150yd. A grid search per boss is too dear for a check every
    // reach action pays, so the GUIDs are kept in the bot's "coa room boss memo" value for ROOM_BOSS_MEMO_MS and only
    // resolved and re-tested on each read.
    std::vector<ObjectGuid> const& SleepingRoomBossesNear(Player* bot, AiObjectContext* context)
    {
        constexpr uint32 ROOM_BOSS_MEMO_MS = 2000;
        CoaRoomBossMemo& m = context->GetValue<CoaRoomBossMemo&>("coa room boss memo")->RefGet();
        uint32 const now = getMSTime();
        if (m.stampMs == 0 || m.mapId != bot->GetMapId() || m.instanceId != bot->GetInstanceId() ||
            getMSTimeDiff(m.stampMs, now) >= ROOM_BOSS_MEMO_MS)
        {
            m.bosses.clear();
            for (CoaRoomBossChase::RoomBoss const* row : CoaRoomBossChase::ForMap(bot->GetMapId()))
                if (Creature* const boss = bot->FindNearestCreature(row->bossEntry, 150.0f, true))
                    m.bosses.push_back(boss->GetGUID());
            m.stampMs = now != 0 ? now : 1u;
            m.mapId = bot->GetMapId();
            m.instanceId = bot->GetInstanceId();
        }
        return m.bosses;
    }

    // DcEngageGeometry::RoomAggroSphereRadius: the boss's aggro reach on `sizer` plus the path padding, widened by the
    // boss's skirt override.
    float WakeSphere(Player* sizer, Creature* boss, CoaGardesSettings const& settings)
    {
        float const computed =
            CoaRoomBossChase::AggroReachYards(boss->GetAggroRange(sizer), boss->GetCombatReach(),
                                              sizer->GetCombatReach(), settings.aggroRangeMargin,
                                              settings.roomAggroPathPadding);
        return CoaRoomBossChase::WakeSphere(computed,
                                            CoaRoomBossChase::SkirtOverride(boss->GetMapId(), boss->GetEntry()));
    }

    bool ChaseWakesSleepingRoomBoss(Player* bot, AiObjectContext* context, Action* action)
    {
        std::string targetName;
        float range = 0.0f;  // only `reach spell` stops short of its target
        bool chase = false;
        if (auto* reach = dynamic_cast<ReachTargetAction*>(action))
        {
            chase = true;
            targetName = reach->GetTargetName();
            if (action->getName() == "reach spell")
                if (PlayerbotAI* const botAI = GET_PLAYERBOT_AI(bot))
                    range = botAI->GetRange("spell");
        }
        else if (auto* leap = dynamic_cast<CastReachTargetSpellAction*>(action))
        {
            chase = true;
            targetName = leap->GetTargetName();
        }
        if (!chase || !CoaGroupTank::ModuleGuardActs(bot, CoaGardesGate::ROOM_BOSS_CHASE))
            return false;

        Player* tank = CoaGardesUnits::Tank(bot);
        bool const inRun = CoaGroupTank::InDungeonClearRun(bot);
        CoaRoomBossChase::BranchInputs in;
        in.chaseAction = true;
        in.hasTank = tank && tank->IsAlive() && tank->FindMap() == bot->FindMap();
        in.tankIsHuman = CoaGardesUnits::IsHuman(tank);
        in.inDcRun = inRun;
        in.botIsTank = PlayerbotAI::IsTank(bot);
        CoaRoomBossChase::Branch const branch = CoaRoomBossChase::ChooseBranch(in);
        if (branch == CoaRoomBossChase::Branch::None)
            return false;
        // A raid encounter's stand-down hands the fight to the raid strategy.
        if (inRun && CoaGardesRunHooks::IsRaidStandDown(bot))
            return false;

        Unit* const target = context->GetValue<Unit*>(targetName)->Get();
        if (!target || target->FindMap() != bot->FindMap())
            return false;
        CoaGardesSettings const& settings = CoaGardesSettings::Get();

        if (branch == CoaRoomBossChase::Branch::RunBoss)
        {
            Creature* const boss = CoaGardesRunHooks::RoomClearBossOf(tank);
            if (!boss || boss == target || boss->FindMap() != bot->FindMap())
                return false;
            float const chaseEnd = CoaRoomBossChase::ChaseEndToBoss(
                bot->GetPositionX(), bot->GetPositionY(), target->GetPositionX(), target->GetPositionY(),
                boss->GetPositionX(), boss->GetPositionY(), range);
            return CoaRoomBossChase::ChaseWakesSleepingBoss(chaseEnd, WakeSphere(tank, boss, settings),
                                                            boss->IsInCombat());
        }

        for (ObjectGuid const& bossGuid : SleepingRoomBossesNear(bot, context))
        {
            Creature* const boss = ObjectAccessor::GetCreature(*bot, bossGuid);
            if (!boss || !boss->IsAlive() || boss == target || boss->IsInCombat())
                continue;
            float const chaseEnd = CoaRoomBossChase::ChaseEndToBoss(
                bot->GetPositionX(), bot->GetPositionY(), target->GetPositionX(), target->GetPositionY(),
                boss->GetPositionX(), boss->GetPositionY(), range);
            if (CoaRoomBossChase::ChaseWakesSleepingBoss(chaseEnd, WakeSphere(bot, boss, settings), false))
                return true;
        }
        return false;
    }
}

float CoaGardesCombatMultiplier::GetValue(Action* action)
{
    // Instances only (2.9.4): no ban outside a group on a dungeon or raid map.
    if (!action || !botAI || !CoaGroupTank::GuardScope(bot))
        return 1.0f;
    std::string const& name = action->getName();
    if (LyceumChaseLeavesThePack(bot, context, action))
        return 0.0f;
    if (ChaseWakesSleepingRoomBoss(bot, context, action))
        return 0.0f;
    return 1.0f;
}

float CoaGardesNonCombatMultiplier::GetValue(Action* action)
{
    if (!action || !botAI || !CoaGroupTank::GuardScope(bot))
        return 1.0f;
    return 1.0f;
}

// Relevances are Dungeon Clear's (DcRelevance): the Iron Hall hop 57 (above every mover that parks a bot), the
// Lyceum close-up 44 (above every stock combat mover), the combat regroup 29 (above reach spell and the rotation,
// below the heal reposition).
void CoaGardesCombatStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("coa iron hall safe spot", {NextAction("coa iron hall safe spot", 57.0f)}));
    triggers.push_back(new TriggerNode("coa lyceum close up", {NextAction("coa lyceum close up", 44.0f)}));
    triggers.push_back(new TriggerNode("coa combat regroup", {NextAction("coa combat regroup", 29.0f)}));
}

void CoaGardesCombatStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaGardesCombatMultiplier(botAI));
}

void CoaGardesNonCombatStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("coa iron hall safe spot", {NextAction("coa iron hall safe spot", 57.0f)}));
}

void CoaGardesNonCombatStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaGardesNonCombatMultiplier(botAI));
}
