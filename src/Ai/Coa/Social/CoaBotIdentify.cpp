/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotIdentify.h"

#include "CoaAltAddon.h"
#include "CoaBotIdentifyRules.h"
#include "GameTime.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"

#include <mutex>

namespace
{
std::mutex throttleLock;
CoaIdentifyThrottle throttle;
}  // namespace

namespace CoaBotIdentify
{
void OnGroupAddonMessage(Player* asker, uint32 type, std::string const& msg, Group* group)
{
    if (!asker || !group || !CoaIdentifyIsWho(msg) ||
        !CoaIdentifyEnabled(sPlayerbotAIConfig.coaBotIdentify, sPlayerbotAIConfig.coaBotSurname))
        return;

    bool const party = type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER;
    if (!party && type != CHAT_MSG_RAID && type != CHAT_MSG_RAID_LEADER)
        return;

    uint64 const now = GameTime::GetGameTimeMS().count();
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* const member = itr->GetSource();
        if (!member || member == asker || !member->IsInWorld() || IsSelfBot(member))
            continue;
        // Only the members the message reached (PARTY: the asker's subgroup), of the asker's faction.
        if (party && !group->SameSubGroup(asker, member))
            continue;
        if (member->GetTeamId() != asker->GetTeamId())
            continue;

        PlayerbotAI* const botAI = GET_PLAYERBOT_AI(member);
        if (!botAI)
            continue;

        {
            std::lock_guard<std::mutex> guard(throttleLock);
            if (!throttle.Allow(member->GetGUID().GetRawValue(), asker->GetGUID().GetRawValue(), now))
                continue;
        }

        bool const randomBot = sRandomPlayerbotMgr.IsRandomBot(member);
        Player* const master = botAI->GetMaster();
        CoaAltAddon::Send(member, asker, CoaIdentifyAnswer(randomBot, master && master != member ? master->GetName() : ""));
    }
}
}  // namespace CoaBotIdentify
