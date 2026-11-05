/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CoaLeaveInstance.h"

#include "CoaMissionMgr.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GroupScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotOperation.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint64 FollowDelayMs = 2500;
    constexpr uint64 RegroupDelayMs = 3000;

    struct PendingLeave
    {
        uint32 mapId;
        uint32 instanceId;
        uint64 dueMs;
        std::vector<ObjectGuid> bots;
        // Second step: the alts teleported out of a Dungeon Finder group join the player's group again.
        bool regroup = false;
    };

    std::mutex PendingLock;
    // Two queues per player: a regroup must not replace a follow still waiting, nor the other way round.
    std::unordered_map<ObjectGuid, PendingLeave> Pending;
    std::unordered_map<ObjectGuid, PendingLeave> Regroups;
    std::atomic<uint32> PendingCount{0};

    uint64 NowMs() { return GameTime::GetGameTimeMS().count(); }

    void Schedule(ObjectGuid playerGuid, PendingLeave leave)
    {
        std::lock_guard<std::mutex> lock(PendingLock);
        auto& queue = leave.regroup ? Regroups : Pending;
        if (leave.bots.empty())
            queue.erase(playerGuid);
        else
            queue[playerGuid] = std::move(leave);
        PendingCount = uint32(Pending.size() + Regroups.size());
    }

    // Runs in the world thread: the player has left a Dungeon Finder group (the Leave button only teleports out and
    // keeps the group; leaving the group comes after, or instead), and the alts were still in it, outside the player's
    // group (tests of 04/11). Each leaves it and joins the player's group, made by the player when there is none, as
    // before the queue. Taken out of a Dungeon Finder group inside the dungeon, the core sends the alt out of it.
    class RegroupOperation : public PlayerbotOperation
    {
    public:
        RegroupOperation(ObjectGuid playerGuid, std::vector<ObjectGuid> bots)
            : m_playerGuid(playerGuid), m_bots(std::move(bots))
        {
        }

        bool Execute() override
        {
            Player* player = ObjectAccessor::FindPlayer(m_playerGuid);
            if (!player || !player->IsInWorld() || player->InBattleground() || player->InArena())
                return false;
            Group* own = player->GetGroup();
            if (own && (own->isLFGGroup() || own->isBGGroup() || own->isBFGroup()))
                return false;

            uint32 joined = 0;
            for (ObjectGuid const& guid : m_bots)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || !GET_PLAYERBOT_AI(bot))
                    continue;
                if (Group* old = bot->GetGroup())
                {
                    if (!old->isLFGGroup() || old->IsMember(player->GetGUID()))
                        continue;
                    old->RemoveMember(bot->GetGUID(), GROUP_REMOVEMETHOD_LEAVE);
                    if (bot->GetGroup())
                        continue;
                }

                if (!own)
                {
                    own = new Group();
                    if (!own->Create(player))
                    {
                        delete own;
                        return joined > 0;
                    }
                    sGroupMgr->AddGroup(own);
                }
                if (own->IsFull() && !own->isRaidGroup())
                    own->ConvertToRaid();
                if (own->IsFull() || !own->AddMember(bot))
                    continue;
                ++joined;
            }

            LOG_DEBUG("playerbots", "{} left a Dungeon Finder group: {} alts joined the player's group", player->GetName(),
                      joined);
            return joined > 0;
        }

        ObjectGuid GetBotGuid() const override { return m_playerGuid; }
        uint32 GetPriority() const override { return 50; }
        std::string GetName() const override { return "CoaLeaveInstanceRegroup"; }

    private:
        ObjectGuid m_playerGuid;
        std::vector<ObjectGuid> m_bots;
    };

    // Runs in the world thread: the bots are on the instance map, the player on another one.
    class FollowOutOperation : public PlayerbotOperation
    {
    public:
        FollowOutOperation(ObjectGuid playerGuid, PendingLeave leave)
            : m_playerGuid(playerGuid), m_leave(std::move(leave))
        {
        }

        bool Execute() override
        {
            Player* player = ObjectAccessor::FindPlayer(m_playerGuid);
            if (!player || !player->IsInWorld() || player->IsBeingTeleported() || !player->IsAlive())
                return false;
            if (player->InBattleground() || player->InArena() || player->GetMap()->Instanceable())
                return false;

            uint32 moved = 0;
            std::vector<ObjectGuid> regroup;
            for (ObjectGuid const& guid : m_leave.bots)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
                if (!botAI || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive())
                    continue;
                if (bot->GetMapId() != m_leave.mapId || bot->GetInstanceId() != m_leave.instanceId)
                    continue;

                Group* group = bot->GetGroup();
                // AiPlayerbot.LfgKeepRandomBots: they stay with a player still in their group, to queue it again.
                bool const kept = sPlayerbotAIConfig.lfgKeepRandomBots && group && group->IsMember(player->GetGUID());
                if (group && group->isLFGGroup() && sRandomPlayerbotMgr.IsRandomBot(bot) && !kept)
                {
                    botAI->LeaveOrDisbandGroup();
                    bot->TeleportToEntryPoint();
                }
                else
                {
                    bot->TeleportTo(player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                                    player->GetPositionZ(), player->GetOrientation());
                    if (group && group->isLFGGroup() && !group->IsMember(player->GetGUID()))
                        regroup.push_back(guid);
                }
                ++moved;
            }

            if (!regroup.empty())
                Schedule(m_playerGuid, PendingLeave{0, 0, NowMs() + RegroupDelayMs, std::move(regroup), true});

            LOG_DEBUG("playerbots", "{} left instance {} of map {}: {} bots follow", player->GetName(),
                      m_leave.instanceId, m_leave.mapId, moved);
            return moved > 0;
        }

        ObjectGuid GetBotGuid() const override { return m_playerGuid; }
        uint32 GetPriority() const override { return 50; }
        std::string GetName() const override { return "CoaLeaveInstanceFollowOut"; }

    private:
        ObjectGuid m_playerGuid;
        PendingLeave m_leave;
    };
}

void CoaLeaveInstance::OnBeforeTeleport(Player* player, uint32 mapId)
{
    if (!player || !player->IsInWorld() || GET_PLAYERBOT_AI(player) || !player->IsAlive())
        return;

    Map* map = player->GetMap();
    if (!map || !map->IsDungeon() || map->IsBattlegroundOrArena() || mapId == player->GetMapId())
        return;

    // The Dungeon Finder Leave takes the player out of the group before the teleport: when no other player stays
    // in the instance, the bots of a Dungeon Finder group there are the player's group too.
    bool otherPlayer = false;
    for (auto const& ref : map->GetPlayers())
        if (Player* other = ref.GetSource(); other && other != player && !GET_PLAYERBOT_AI(other))
            otherPlayer = true;

    Group* group = player->GetGroup();
    PendingLeave leave{map->GetId(), map->GetInstanceId(), NowMs() + FollowDelayMs, {}};
    for (auto const& ref : map->GetPlayers())
    {
        Player* bot = ref.GetSource();
        PlayerbotAI* botAI = bot && bot != player ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!botAI || !bot->IsAlive() || sCoaMissionMgr.IsOnMission(bot))
            continue;
        Group* botGroup = bot->GetGroup();
        if (botAI->GetMaster() == player || (group && group->IsMember(bot->GetGUID())) ||
            (!otherPlayer && botGroup && botGroup->isLFGGroup()))
            leave.bots.push_back(bot->GetGUID());
    }

    Schedule(player->GetGUID(), std::move(leave));
}

void CoaLeaveInstance::OnUpdate(Player* player)
{
    if (!PendingCount || !player || GET_PLAYERBOT_AI(player))
        return;

    PendingLeave leave;
    {
        std::lock_guard<std::mutex> lock(PendingLock);
        if (player->IsBeingTeleported() || !player->IsInWorld())
            return;
        uint64 const now = NowMs();
        auto itr = Regroups.find(player->GetGUID());
        if (itr != Regroups.end() && now >= itr->second.dueMs)
        {
            leave = std::move(itr->second);
            Regroups.erase(itr);
        }
        else
        {
            itr = Pending.find(player->GetGUID());
            if (itr == Pending.end() || now < itr->second.dueMs)
                return;
            leave = std::move(itr->second);
            Pending.erase(itr);
        }
        PendingCount = uint32(Pending.size() + Regroups.size());
    }

    if (leave.regroup)
    {
        PlayerbotWorldThreadProcessor::instance().QueueOperation(
            std::make_unique<RegroupOperation>(player->GetGUID(), std::move(leave.bots)));
        return;
    }

    // Back in the same instance (a teleport inside it, or the player came back in time): nothing to follow.
    if (player->GetMapId() == leave.mapId && player->GetInstanceId() == leave.instanceId)
        return;

    PlayerbotWorldThreadProcessor::instance().QueueOperation(
        std::make_unique<FollowOutOperation>(player->GetGUID(), std::move(leave)));
}

// A player leaving a Dungeon Finder group, by the Leave of the party frame or after the Leave button that only
// teleported out: the alts still in it rejoin the player (RegroupOperation), as the player is no longer with them.
class CoaLeaveInstanceGroupScript : public GroupScript
{
public:
    CoaLeaveInstanceGroupScript() : GroupScript("CoaLeaveInstanceGroupScript", {GROUPHOOK_ON_REMOVE_MEMBER}) {}

    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod /*method*/, ObjectGuid /*kicker*/,
                        char const* /*reason*/) override
    {
        if (!group || !group->isLFGGroup())
            return;
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (!player || GET_PLAYERBOT_AI(player))
            return;

        PendingLeave regroup{0, 0, NowMs() + RegroupDelayMs, {}, true};
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* bot = ref->GetSource();
            PlayerbotAI* botAI = bot && bot != player ? GET_PLAYERBOT_AI(bot) : nullptr;
            if (botAI && botAI->GetMaster() == player && !sRandomPlayerbotMgr.IsRandomBot(bot) &&
                !sCoaMissionMgr.IsOnMission(bot))
                regroup.bots.push_back(bot->GetGUID());
        }
        Schedule(guid, std::move(regroup));
    }
};

void AddSC_coa_leave_instance()
{
    new CoaLeaveInstanceGroupScript();
}
