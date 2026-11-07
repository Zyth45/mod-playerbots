/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGroupTank.h"

#include "AiObjectContext.h"
#include "CoaPlayerMainTank.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

#include <set>

namespace
{
    CoaGroupTank::Provider ActiveRunLeader;
    std::set<std::string> HandledByDungeonClear;

    bool Usable(Player const* tank, Player const* member)
    {
        return tank && tank->IsInWorld() && tank->IsAlive() && tank->FindMap() && tank->FindMap() == member->FindMap();
    }

    // One provider read per bot tick: the bot's "coa run leader" value; a human (no AI) reads the provider.
    CoaGroupTank::RunLeaderMemo RunLeader(Player const* member)
    {
        if (!ActiveRunLeader || !member)
            return {};
        if (PlayerbotAI* ai = GET_PLAYERBOT_AI(const_cast<Player*>(member)))
            if (AiObjectContext* context = ai->GetAiObjectContext())
                if (Value<CoaGroupTank::RunLeaderMemo>* value =
                        context->GetValue<CoaGroupTank::RunLeaderMemo>("coa run leader"))
                    return value->Get();
        return CoaGroupTank::ReadRunLeader(member);
    }
}

CoaGroupTank::RunLeaderMemo CoaGroupTank::ReadRunLeader(Player const* member)
{
    RunLeaderMemo memo;
    if (!ActiveRunLeader || !member)
        return memo;
    if (Player* leader = ActiveRunLeader(member))
    {
        memo.inRun = true;
        memo.leader = leader->GetGUID();
    }
    return memo;
}

void CoaGroupTank::SetProvider(Provider provider)
{
    ActiveRunLeader = std::move(provider);
}

Player* CoaGroupTank::Reference(Player const* member)
{
    if (!member || !member->IsInWorld())
        return nullptr;
    if (RunLeaderMemo const run = RunLeader(member); run.inRun)
        if (Player* leader = ObjectAccessor::GetPlayer(*member, run.leader); Usable(leader, member))
            return leader;
    Group* group = const_cast<Player*>(member)->GetGroup();
    if (!group)
        return nullptr;
    if (Player* player = CoaPlayerMainTank::FlaggedInGroup(group, member); Usable(player, member))
        return player;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.flags & MEMBER_FLAG_MAINTANK)
            if (Player* flagged = ObjectAccessor::FindPlayer(slot.guid); Usable(flagged, member))
                return flagged;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* tank = ref->GetSource(); Usable(tank, member) && PlayerbotAI::IsTank(tank))
            return tank;
    return nullptr;
}

bool CoaGroupTank::InDungeonClearRun(Player const* member)
{
    return RunLeader(member).inRun;
}

void CoaGroupTank::SetHandledByDungeonClear(std::string const& guard, bool handled)
{
    if (handled)
        HandledByDungeonClear.insert(guard);
    else
        HandledByDungeonClear.erase(guard);
}

bool CoaGroupTank::GuardScope(Player const* member)
{
    if (!member || !member->GetGroup())
        return false;
    Map const* map = member->FindMap();
    return map && map->IsDungeon();
}

bool CoaGroupTank::ModuleGuardActs(Player const* member, std::string const& guard)
{
    if (!GuardScope(member))
        return false;
    return !HandledByDungeonClear.count(guard) || !InDungeonClearRun(member);
}
