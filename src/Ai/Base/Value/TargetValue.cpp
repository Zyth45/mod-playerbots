/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TargetValue.h"
#include "CombatManager.h"
#include "Helpers.h"
#include "LastMovementValue.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "ScriptedCreature.h"
#include "Strategy.h"
#include "ThreatManager.h"

GuidSet GatherStrategyTargetExclusions(PlayerbotAI* botAI, TargetValueExclusionType type)
{
    GuidSet exclusions;
    if (!botAI || !botAI->HasTargetExclusions())
        return exclusions;

    for (auto const& strategyName : botAI->GetStrategies(BOT_STATE_COMBAT))
    {
        Strategy* strategy = botAI->GetStrategy(strategyName, BOT_STATE_COMBAT);
        if (!strategy)
            continue;

        strategy->AppendTargetExclusions(exclusions, type);
    }

    return exclusions;
}

bool HasEnoughAoeTargets(PlayerbotAI* botAI, Position const& center, float range, uint32 minCount)
{
    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    auto const isInRange = [&center, range](Unit* unit)
    { return unit && unit->IsAlive() && unit->GetDistance(center) <= range; };

    uint32 count = 0;
    for (ObjectGuid const guid : attackers)
    {
        if (isInRange(botAI->GetUnit(guid)))
            ++count;
    }

    if (count < minCount || !sPlayerbotAIConfig.coaExclusionsEverywhere || !botAI->HasTargetExclusions())
        return count >= minCount;

    GuidSet const exclusions = GatherStrategyTargetExclusions(botAI, TargetValueExclusionType::Aoe);
    for (ObjectGuid const guid : attackers)
    {
        if (exclusions.find(guid) != exclusions.end() && isInRange(botAI->GetUnit(guid)))
            --count;
    }

    return count >= minCount;
}

Unit* FindTargetStrategy::GetResult() { return result; }

Unit* TargetValue::FindTarget(FindTargetStrategy* strategy, GuidSet const& exclusions)
{
    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || exclusions.find(guid) != exclusions.end())
            continue;

        ThreatManager& threatMgr = unit->GetThreatMgr();
        strategy->CheckAttacker(unit, &threatMgr);
    }

    return strategy->GetResult();
}

bool FindNonCcTargetStrategy::IsCcTarget(Unit* attacker)
{
    if (Group* group = botAI->GetBot()->GetGroup())
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = ObjectAccessor::FindPlayer(itr->guid);
            if (!member || !member->IsAlive())
                continue;

            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(member))
            {
                if (botAI->GetAiObjectContext()->GetValue<Unit*>("rti cc target")->Get() == attacker)
                    return true;

                std::string const rti = botAI->GetAiObjectContext()->GetValue<std::string>("rti cc")->Get();
                int32 index = RtiTargetValue::GetRtiIndex(rti);
                if (index != -1)
                {
                    if (ObjectGuid guid = group->GetTargetIcon(index))
                        if (attacker->GetGUID() == guid)
                            return true;
                }
            }
        }

        if (ObjectGuid guid = group->GetTargetIcon(4))
            if (attacker->GetGUID() == guid)
                return true;
    }

    return false;
}

void FindTargetStrategy::GetPlayerCount(Unit* creature, uint32* tankCount, uint32* dpsCount)
{
    Player* bot = botAI->GetBot();
    if (tankCountCache.find(creature) != tankCountCache.end())
    {
        *tankCount = tankCountCache[creature];
        *dpsCount = dpsCountCache[creature];
        return;
    }

    *tankCount = 0;
    *dpsCount = 0;

    Unit::AttackerSet attackers(creature->getAttackers());
    for (Unit* attacker : attackers)
    {
        if (!attacker || !attacker->IsAlive() || attacker == bot)
            continue;

        Player* player = attacker->ToPlayer();
        if (!player)
            continue;

        if (botAI->IsTank(player))
            ++(*tankCount);
        else
            ++(*dpsCount);
    }

    tankCountCache[creature] = *tankCount;
    dpsCountCache[creature] = *dpsCount;
}

bool IsFleeingForAssistance(Unit* unit)
{
    if (!unit || !unit->IsCreature())
        return false;

    switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
        case ASSISTANCE_MOTION_TYPE:
        case ASSISTANCE_DISTRACT_MOTION_TYPE:
        case TIMED_FLEEING_MOTION_TYPE:
            return true;
        default:
            return false;
    }
}

TargetPriority FindTargetStrategy::GetPriority(Unit* attacker)
{
    Player* bot = botAI->GetBot();
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid guid = group->GetTargetIcon(7);
        if (guid && attacker->GetGUID() == guid)
            return TargetPriority::Marked;
    }

    GuidVector prioritizedTargets = botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Get();
    for (ObjectGuid targetGuid : prioritizedTargets)
        if (targetGuid && attacker->GetGUID() == targetGuid)
            return TargetPriority::Marked;

    if (sPlayerbotAIConfig.coaFocusRunners && IsFleeingForAssistance(attacker) && bot->GetMap()->IsDungeon())
    {
        Creature* creature = attacker->ToCreature();
        if (!creature->isWorldBoss() && !creature->IsDungeonBoss())
            return TargetPriority::FleeingForAssistance;
    }

    return TargetPriority::Normal;
}

bool FindTargetStrategy::IsHighPriority(Unit* attacker) { return GetPriority(attacker) == TargetPriority::Marked; }

bool FindTargetStrategy::CheckPriority(Unit* attacker)
{
    if (highestPriority == TargetPriority::Marked)
        return true;

    TargetPriority const priority = GetPriority(attacker);
    if (priority > highestPriority)
    {
        highestPriority = priority;
        result = attacker;
    }

    return highestPriority != TargetPriority::Normal;
}

WorldPosition LastLongMoveValue::Calculate()
{
    LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
    if (lastMove.lastPath.empty())
        return WorldPosition();

    return lastMove.lastPath.getBack();
}

WorldPosition HomeBindValue::Calculate()
{
    return WorldPosition(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ, 0.f);
}

Unit* FindTargetValue::Calculate()
{
    if (qualifier == "")
    {
        return nullptr;
    }
    Group* group = bot->GetGroup();
    if (!group)
    {
        return nullptr;
    }
    for (auto const& [guid, ref] : bot->GetThreatMgr().GetThreatenedByMeList())
    {
        Unit* unit = ref->GetOwner();
        if (!unit)
            continue;

        if (CoaNameIs(qualifier, CoaNameKey(unit->GetName())))
            return unit;
    }

    return nullptr;
}

void FindBossTargetStrategy::CheckAttacker(Unit* attacker, ThreatManager* /*threatManager*/)
{
    UnitAI* unitAI = attacker->GetAI();
    BossAI* bossAI = dynamic_cast<BossAI*>(unitAI);
    if (bossAI)
    {
        result = attacker;
    }
}

Unit* BossTargetValue::Calculate()
{
    FindBossTargetStrategy strategy(botAI);
    return FindTarget(&strategy);
}
