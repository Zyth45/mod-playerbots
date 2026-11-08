/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaWorldBots.h"

#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace CoaWorld
{
    namespace
    {
        // A bot appearing or vanishing closer than this to a real player would be seen.
        constexpr float UNSEEN_RANGE = 150.0f;
        constexpr float ARRIVAL_UNSEEN_RANGE = 80.0f;

        struct Borrowed
        {
            ObjectGuid guid;
            WorldLocation home;
            bool teleported = false;
        };

        std::unordered_map<uint32, std::vector<Borrowed>> g_borrowed;

        bool IsBorrowed(ObjectGuid guid)
        {
            for (auto const& [owner, bots] : g_borrowed)
                for (Borrowed const& b : bots)
                    if (b.guid == guid)
                        return true;
            return false;
        }

        bool RealPlayerNear(uint32 mapId, Position const& pos, float range)
        {
            Map* map = sMapMgr->FindBaseMap(mapId);
            if (!map)
                return false;
            for (MapReference const& ref : map->GetPlayers())
            {
                Player* player = ref.GetSource();
                if (player && !GET_PLAYERBOT_AI(player) && player->GetExactDist(&pos) < range)
                    return true;
            }
            return false;
        }

        bool IsFree(Player* bot)
        {
            if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() ||
                bot->IsBeingTeleported())
                return false;
            if (bot->GetGroup() || bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
                return false;
            if (!bot->GetMap() || bot->GetMap()->Instanceable() || IsBorrowed(bot->GetGUID()))
                return false;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            return botAI && !botAI->GetMaster();
        }
    }

    uint32 OnlineRandomBots()
    {
        uint32 online = 0;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
            if (bot && bot->IsInWorld())
                ++online;
        return online;
    }

    uint32 BotBudget(uint32 pct, uint32 max)
    {
        return std::min<uint32>(max, OnlineRandomBots() * std::min<uint32>(pct, 100) / 100);
    }

    uint32 BorrowBots(uint32 owner, BorrowRequest const& request)
    {
        if (!request.count)
            return 0;

        struct Candidate
        {
            Player* bot;
            float dist;
        };
        std::vector<Candidate> candidates;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!IsFree(bot))
                continue;
            if (bot->GetLevel() < request.minLevel || bot->GetLevel() > request.maxLevel)
                continue;
            if (request.team != TEAM_NEUTRAL && bot->GetTeamId() != request.team)
                continue;
            float dist = bot->GetMapId() == request.mapId ? bot->GetExactDist(&request.goal) : 1e9f;
            candidates.push_back({ bot, dist });
        }

        std::sort(candidates.begin(), candidates.end(),
            [](Candidate const& a, Candidate const& b) { return a.dist < b.dist; });

        // Half and half when both sides should come (the rounding goes to whoever has more bots).
        uint32 perTeam[2] = { request.count, request.count };
        if (request.team == TEAM_NEUTRAL && request.balanceTeams)
        {
            perTeam[TEAM_ALLIANCE] = (request.count + 1) / 2;
            perTeam[TEAM_HORDE] = request.count / 2;
        }

        bool arrivalSeen = RealPlayerNear(request.mapId, request.rally, ARRIVAL_UNSEEN_RANGE);
        WorldPosition goal(request.mapId, request.goal.GetPositionX(), request.goal.GetPositionY(),
            request.goal.GetPositionZ());
        std::vector<Borrowed>& mine = g_borrowed[owner];
        uint32 got = 0;

        for (Candidate const& c : candidates)
        {
            if (got >= request.count)
                break;

            Player* bot = c.bot;
            TeamId team = bot->GetTeamId();
            if (team > TEAM_HORDE || !perTeam[team])
                continue;

            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            Borrowed borrowed{ bot->GetGUID(), bot->GetWorldLocation(), false };

            if (c.dist <= request.nearRadius)
            {
                // Near: walks there through its usual travel.
                botAI->rpgInfo.ChangeToGoGrind(goal);
            }
            else
            {
                // Far: brought to the rally point, never under the eyes of a real player.
                if (!request.teleport || arrivalSeen || botAI->HasPlayerNearby(UNSEEN_RANGE))
                    continue;

                float angle = frand(0.0f, 2.0f * float(M_PI));
                float dist = frand(0.0f, 8.0f);
                float x = request.rally.GetPositionX() + dist * std::cos(angle);
                float y = request.rally.GetPositionY() + dist * std::sin(angle);

                bot->GetMotionMaster()->Clear();
                botAI->Reset(true);
                bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                if (!bot->TeleportTo(request.mapId, x, y, request.rally.GetPositionZ(), request.rally.GetOrientation()))
                    continue;
                botAI->rpgInfo.ChangeToGoGrind(goal);
                borrowed.teleported = true;
            }

            mine.push_back(borrowed);
            --perTeam[team];
            ++got;
        }
        return got;
    }

    void ReleaseBots(uint32 owner, bool returnHome)
    {
        auto itr = g_borrowed.find(owner);
        if (itr == g_borrowed.end())
            return;

        for (Borrowed const& b : itr->second)
        {
            Player* bot = ObjectAccessor::FindPlayer(b.guid);
            if (!bot || !bot->IsInWorld())
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI || botAI->GetMaster())
                continue;

            // Still heading there or wandering about the spot: something else to do.
            NewRpgStatus status = botAI->rpgInfo.GetStatus();
            if (status == RPG_GO_GRIND || status == RPG_WANDER_RANDOM)
                botAI->rpgInfo.ChangeToIdle();

            if (b.teleported && returnHome && bot->IsAlive() && !bot->IsInCombat() && !bot->GetGroup() &&
                !bot->IsBeingTeleported() && !botAI->HasPlayerNearby(UNSEEN_RANGE) &&
                !RealPlayerNear(b.home.GetMapId(), b.home, ARRIVAL_UNSEEN_RANGE))
            {
                bot->GetMotionMaster()->Clear();
                botAI->Reset(true);
                bot->TeleportTo(b.home);
            }
        }
        g_borrowed.erase(itr);
    }

    uint32 BorrowedCount(uint32 owner)
    {
        auto itr = g_borrowed.find(owner);
        return itr != g_borrowed.end() ? uint32(itr->second.size()) : 0;
    }

    uint32 BorrowedTotal()
    {
        uint32 n = 0;
        for (auto const& [owner, bots] : g_borrowed)
            n += bots.size();
        return n;
    }
}
