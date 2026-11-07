/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGroupTank.h"

#include "CoaPlayerMainTank.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"

#include <set>

namespace
{
    CoaGroupTank::Provider ActiveRunLeader;
    std::set<std::string> HandledByDungeonClear;

    bool Usable(Player const* tank, Player const* member)
    {
        return tank && tank->IsInWorld() && tank->IsAlive() && tank->FindMap() && tank->FindMap() == member->FindMap();
    }
}

void CoaGroupTank::SetProvider(Provider provider)
{
    ActiveRunLeader = std::move(provider);
}

Player* CoaGroupTank::Reference(Player const* member)
{
    if (!member || !member->IsInWorld())
        return nullptr;
    if (ActiveRunLeader)
        if (Player* leader = ActiveRunLeader(member); Usable(leader, member))
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
    return member && ActiveRunLeader && ActiveRunLeader(member) != nullptr;
}

void CoaGroupTank::SetHandledByDungeonClear(std::string const& guard, bool handled)
{
    if (handled)
        HandledByDungeonClear.insert(guard);
    else
        HandledByDungeonClear.erase(guard);
}

bool CoaGroupTank::ModuleGuardActs(Player const* member, std::string const& guard)
{
    return !HandledByDungeonClear.count(guard) || !InDungeonClearRun(member);
}
