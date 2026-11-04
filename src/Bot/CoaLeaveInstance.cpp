/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CoaLeaveInstance.h"

#include "CoaMissionMgr.h"
#include "GameTime.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotOperation.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint64 FollowDelayMs = 2500;

    struct PendingLeave
    {
        uint32 mapId;
        uint32 instanceId;
        uint64 dueMs;
        std::vector<ObjectGuid> bots;
    };

    std::mutex PendingLock;
    std::unordered_map<ObjectGuid, PendingLeave> Pending;
    std::atomic<uint32> PendingCount{0};

    uint64 NowMs() { return GameTime::GetGameTimeMS().count(); }

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
            for (ObjectGuid const& guid : m_leave.bots)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
                if (!botAI || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive())
                    continue;
                if (bot->GetMapId() != m_leave.mapId || bot->GetInstanceId() != m_leave.instanceId)
                    continue;

                Group* group = bot->GetGroup();
                if (group && group->isLFGGroup() && sRandomPlayerbotMgr.IsRandomBot(bot))
                {
                    botAI->LeaveOrDisbandGroup();
                    bot->TeleportToEntryPoint();
                }
                else
                {
                    bot->TeleportTo(player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                                    player->GetPositionZ(), player->GetOrientation());
                }
                ++moved;
            }

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

    std::lock_guard<std::mutex> lock(PendingLock);
    if (leave.bots.empty())
        Pending.erase(player->GetGUID());
    else
        Pending[player->GetGUID()] = std::move(leave);
    PendingCount = uint32(Pending.size());
}

void CoaLeaveInstance::OnUpdate(Player* player)
{
    if (!PendingCount || !player || GET_PLAYERBOT_AI(player))
        return;

    PendingLeave leave;
    {
        std::lock_guard<std::mutex> lock(PendingLock);
        auto itr = Pending.find(player->GetGUID());
        if (itr == Pending.end() || NowMs() < itr->second.dueMs)
            return;
        if (player->IsBeingTeleported() || !player->IsInWorld())
            return;
        leave = std::move(itr->second);
        Pending.erase(itr);
        PendingCount = uint32(Pending.size());
    }

    // Back in the same instance (a teleport inside it, or the player came back in time): nothing to follow.
    if (player->GetMapId() == leave.mapId && player->GetInstanceId() == leave.instanceId)
        return;

    PlayerbotWorldThreadProcessor::instance().QueueOperation(
        std::make_unique<FollowOutOperation>(player->GetGUID(), std::move(leave)));
}
