/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaWorldBots.h"

#include "CoaLive.h"
#include "WorldSession.h"

#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
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
        std::unordered_set<ObjectGuid> g_borrowedSet;  // every bot of g_borrowed, for IsBorrowed

        // World thread only (events' OnUpdate and RandomPlayerbotMgr::UpdateAIInternal).
        std::unordered_map<uint32, time_t> g_held;
        bool g_anyHeld = false;

        bool IsBorrowed(ObjectGuid guid)
        {
            return g_borrowedSet.count(guid) != 0;
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

        bool IsFreeImpl(Player* bot)
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

        // Near: walks there through its usual travel. Far: brought to the rally point, never under the
        // eyes of a real player.
        bool SendOne(uint32 owner, Player* bot, BorrowRequest const& request, float dist, bool arrivalSeen)
        {
            WorldPosition goal(request.mapId, request.goal.GetPositionX(), request.goal.GetPositionY(),
                request.goal.GetPositionZ());
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            Borrowed borrowed{ bot->GetGUID(), bot->GetWorldLocation(), false };

            if (dist <= request.nearRadius)
            {
                // Near: walks there through its usual travel.
                botAI->rpgInfo.ChangeToGoGrind(goal);
            }
            else if (request.holdFar && !request.teleport)
            {
                // Far, held: stays where it is until the event brings it (BringBot).
            }
            else
            {
                // Far: brought to the rally point, never under the eyes of a real player.
                if (!request.teleport || arrivalSeen || botAI->HasPlayerNearby(UNSEEN_RANGE))
                    return false;

                float angle = frand(0.0f, 2.0f * float(M_PI));
                float spread = frand(0.0f, 8.0f);
                float x = request.rally.GetPositionX() + spread * std::cos(angle);
                float y = request.rally.GetPositionY() + spread * std::sin(angle);

                bot->GetMotionMaster()->Clear();
                botAI->Reset(true);
                bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                if (!bot->TeleportTo(request.mapId, x, y, request.rally.GetPositionZ(), request.rally.GetOrientation()))
                    return false;
                botAI->rpgInfo.ChangeToGoGrind(goal);
                borrowed.teleported = true;
            }

            g_borrowed[owner].push_back(borrowed);
            g_borrowedSet.insert(borrowed.guid);
            return true;
        }
    }

    // Same auras as the core's rulesets (AscensionRulesets.cpp) and EnemyPlayerValue's check.
    bool HasPvpRuleset(Player* player)
    {
        return player && (player->HasAura(1004019) || (player->HasAura(1004119) && !player->HasAura(9931032)));
    }

    RandomBotRange RandomBots()
    {
        return { sRandomPlayerbotMgr.GetPlayerBotsBegin(), sRandomPlayerbotMgr.GetPlayerBotsEnd() };
    }

    uint32 OnlineRandomBots()
    {
        uint32 online = 0;
        for (auto const& [guid, bot] : RandomBots())
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
        for (auto const& [guid, bot] : RandomBots())
        {
            if (!IsFreeImpl(bot))
                continue;
            if (bot->GetLevel() < request.minLevel || bot->GetLevel() > request.maxLevel)
                continue;
            if (request.team != TEAM_NEUTRAL && bot->GetTeamId() != request.team)
                continue;
            if (request.filter && !request.filter(bot))
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
        uint32 got = 0;

        for (Candidate const& c : candidates)
        {
            if (got >= request.count)
                break;

            Player* bot = c.bot;
            TeamId team = bot->GetTeamId();
            if (team > TEAM_HORDE || !perTeam[team])
                continue;

            if (!SendOne(owner, bot, request, c.dist, arrivalSeen))
                continue;

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
            g_borrowedSet.erase(b.guid);
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

    bool IsFreeBot(Player* bot)
    {
        return IsFreeImpl(bot);
    }

    bool SendBot(uint32 owner, Player* bot, BorrowRequest const& request)
    {
        if (!IsFreeImpl(bot))
            return false;
        float dist = bot->GetMapId() == request.mapId ? bot->GetExactDist(&request.goal) : 1e9f;
        return SendOne(owner, bot, request, dist, RealPlayerNear(request.mapId, request.rally, ARRIVAL_UNSEEN_RANGE));
    }

    bool BringBot(uint32 owner, ObjectGuid guid, uint32 mapId, Position const& at, Position const& goal)
    {
        auto itr = g_borrowed.find(owner);
        if (itr == g_borrowed.end())
            return false;
        auto b = std::find_if(itr->second.begin(), itr->second.end(), [guid](Borrowed const& x) { return x.guid == guid; });
        if (b == itr->second.end())
            return false;
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() ||
            bot->IsBeingTeleported())
            return false;
        if (bot->GetGroup() || bot->InBattleground() || bot->InArena() || !bot->GetMap() || bot->GetMap()->Instanceable())
            return false;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || botAI->GetMaster() || botAI->HasPlayerNearby(UNSEEN_RANGE))
            return false;

        float const facing = std::atan2(goal.GetPositionY() - at.GetPositionY(), goal.GetPositionX() - at.GetPositionX());
        bot->GetMotionMaster()->Clear();
        botAI->Reset(true);
        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        if (!bot->TeleportTo(mapId, at.GetPositionX(), at.GetPositionY(), at.GetPositionZ(), facing))
            return false;
        botAI->rpgInfo.ChangeToGoGrind(WorldPosition(mapId, goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ()));
        b->teleported = true;
        return true;
    }

    void ForgetBot(uint32 owner, ObjectGuid guid)
    {
        auto itr = g_borrowed.find(owner);
        if (itr == g_borrowed.end())
            return;
        std::erase_if(itr->second, [guid](Borrowed const& b) { return b.guid == guid; });
        g_borrowedSet.erase(guid);
        if (itr->second.empty())
            g_borrowed.erase(itr);
    }

    std::vector<ObjectGuid> BorrowedBots(uint32 owner)
    {
        std::vector<ObjectGuid> out;
        auto itr = g_borrowed.find(owner);
        if (itr != g_borrowed.end())
            for (Borrowed const& b : itr->second)
                out.push_back(b.guid);
        return out;
    }

    void HoldBot(uint32 lowGuid, time_t until)
    {
        g_held[lowGuid] = until;
        g_anyHeld = true;
    }

    void ReleaseHold(uint32 lowGuid)
    {
        g_held.erase(lowGuid);
        g_anyHeld = !g_held.empty();
    }

    bool IsHeld(uint32 lowGuid)
    {
        if (!g_anyHeld)
            return false;
        auto itr = g_held.find(lowGuid);
        if (itr == g_held.end())
            return false;
        if (itr->second > time(nullptr))
            return true;
        g_held.erase(itr);
        g_anyHeld = !g_held.empty();
        return false;
    }

    std::string WorldLine(Player* reader, char const* situation, Slots const& slots, std::string const& fallback)
    {
        std::string line = reader ? CoaLive::SystemLine(reader, situation, slots) : std::string();
        return line.empty() ? fallback : line;
    }

    bool BotLine(Player* bot, char const* situation, Slots const& slots)
    {
        return bot && CoaLive::SayLine(bot, situation, slots);
    }

    void BorrowedSay(uint32 owner, size_t from, char const* situation, Slots const& slots, uint32 most)
    {
        auto itr = g_borrowed.find(owner);
        if (itr == g_borrowed.end())
            return;
        uint32 said = 0;
        for (size_t i = from; i < itr->second.size() && said < most; ++i)
            if (Player* bot = ObjectAccessor::FindPlayer(itr->second[i].guid))
                if (bot->IsInWorld() && BotLine(bot, situation, slots))
                    ++said;
    }

    bool BotChatOn() { return CoaLive::ChatOn(); }

    bool BotsFrench()
    {
        int32 const forced = sPlayerbotAIConfig.botTextLocale;
        uint32 const locale = forced >= 0 && forced < TOTAL_LOCALES ? uint32(forced)
                                                                    : uint32(PlayerbotTextMgr::instance().GetLocalePriority());
        return locale == LOCALE_frFR;
    }

    // System lines to a real player: in that player's own language (not the bots').
    std::string EventName(Player* reader, std::string const& key)
    {
        bool fr = reader && reader->GetSession() && reader->GetSession()->GetSessionDbLocaleIndex() == LOCALE_frFR;
        if (key == "scourge")
            return fr ? "le Fl\u00e9au" : "the Scourge";
        if (key == "legion")
            return fr ? "la L\u00e9gion ardente" : "the Burning Legion";
        return fr ? "l'essaim qiraji" : "the Qiraji swarm";
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

// Called for every random bot on every cycle of RandomPlayerbotMgr (world thread): with nobody held,
// a plain bool and nothing else.
bool CoaWorld::IsBorrowedBot(ObjectGuid guid)
{
    return IsBorrowed(guid);
}

bool CoaWorldIsBotHeld(uint32 lowGuid)
{
    return CoaWorld::IsHeld(lowGuid);
}
