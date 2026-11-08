/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCombatGroupTank.h"

#include <atomic>

#include "CoaPlayerMainTank.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"

namespace
{
    std::atomic<CoaCombatGroupTank::RunTankProvider> Provider{nullptr};

    bool Usable(Player const* candidate, Player const* bot)
    {
        return candidate && candidate->IsInWorld() && candidate->IsAlive() && candidate->GetMap() == bot->GetMap();
    }
}

void CoaCombatGroupTank::SetRunTankProvider(RunTankProvider provider) { Provider.store(provider); }

Player* CoaCombatGroupTank::Of(Player* bot)
{
    if (!bot)
        return nullptr;

    if (RunTankProvider provider = Provider.load())
    {
        Player* runTank = provider(bot);
        if (Usable(runTank, bot))
            return runTank;
    }

    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    Player* playerTank = CoaPlayerMainTank::FlaggedInGroup(group, bot);
    if (Usable(playerTank, bot))
        return playerTank;

    for (Group::member_citerator itr = group->GetMemberSlots().begin(); itr != group->GetMemberSlots().end(); ++itr)
    {
        if (itr->flags & MEMBER_FLAG_MAINTANK)
        {
            Player* flagged = ObjectAccessor::FindPlayer(itr->guid);
            if (Usable(flagged, bot))
                return flagged;
            break;  // only the first flagged slot counts
        }
    }

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (Usable(member, bot) && PlayerbotAI::IsTank(member))
            return member;
    }

    return nullptr;
}
