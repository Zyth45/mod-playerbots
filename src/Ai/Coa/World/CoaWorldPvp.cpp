/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: world PvP and the defence of the capitals.
 *
 * Skirmishes: now and then, the posts of a contested zone send their fighters at each other
 * (Southshore and Tarren Mill, Astranaar and Splintertree, Refuge Pointe and Hammerfall, the Rebel
 * Camp and Grom'gol). Random bots of both sides, of the zone's level and under a PvP ruleset of CoA
 * (the only ones a bot starts a fight with), leave their post (brought there out of sight if they
 * were far) and walk to the middle ground, where they meet. The real players of that level are told.
 *
 * Capital defence: when enough real players of the other side are inside a capital, random bots of
 * the capital's side and of the attackers' level rally inside the city (out of sight) and go for the
 * attackers, following them as they move; the city's side is told. They stand down when the city
 * has been clear for two minutes.
 *
 * Written for CoA from the behaviour wanted in the 3.0 plan (skirmish hot spots, reactive defence of
 * the capitals); no code taken from any other project. Coordinates: CoA game_tele. World thread only.
 * Config: AiPlayerbot.CoaWorld.WorldPvp.*, off by default.
 */

#include "CoaWorldBots.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    using namespace CoaWorld;

    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.WorldPvp.";
    constexpr uint32 MINUTE_MS = MINUTE * IN_MILLISECONDS;
    constexpr uint32 CHECK_MS = 30 * IN_MILLISECONDS;

    struct Front
    {
        char const* name;        // for the message
        uint32 mapId;
        uint8 minLevel;
        uint8 maxLevel;
        Position alliancePost;
        Position hordePost;
    };

    std::vector<Front> const FRONTS = {
        { "between Southshore and Tarren Mill", 0, 20, 32,
            { -853.2f, -533.5f, 10.0f, 0.24f }, { -34.1f, -923.4f, 54.6f, 0.15f } },
        { "between Astranaar and Splintertree Post", 1, 20, 32,
            { 2676.2f, -422.9f, 107.1f, 0.65f }, { 2270.9f, -2538.2f, 93.9f, 0.06f } },
        { "between Refuge Pointe and Hammerfall", 0, 30, 42,
            { -1246.6f, -2529.3f, 20.6f, 0.74f }, { -941.0f, -3526.7f, 70.9f, 3.49f } },
        { "between the Rebel Camp and Grom'gol", 0, 33, 45,
            { -11322.4f, -202.5f, 75.6f, 0.43f }, { -12388.9f, 172.6f, 2.8f, 1.92f } },
    };

    struct Capital
    {
        char const* name;
        uint32 zoneId;
        uint32 mapId;
        TeamId team;
        Position rally;
    };

    std::vector<Capital> const CAPITALS = {
        { "Stormwind", 1519, 0, TEAM_ALLIANCE, { -8833.4f, 628.6f, 94.0f, 1.07f } },
        { "Ironforge", 1537, 0, TEAM_ALLIANCE, { -4918.9f, -940.4f, 501.6f, 5.42f } },
        { "Darnassus", 1657, 1, TEAM_ALLIANCE, { 9949.6f, 2284.2f, 1341.4f, 1.60f } },
        { "Orgrimmar", 1637, 1, TEAM_HORDE, { 1629.9f, -4373.6f, 31.6f, 3.70f } },
        { "Undercity", 1497, 0, TEAM_HORDE, { 1584.1f, 240.3f, -52.2f, 0.04f } },
        { "Thunder Bluff", 1638, 1, TEAM_HORDE, { -1277.4f, 124.8f, 131.3f, 5.22f } },
    };

    struct Settings
    {
        bool enabled = false;
        bool skirmishEnabled = true;
        uint32 skirmishIntervalMin = 45;
        uint32 skirmishIntervalMax = 90;
        uint32 skirmishDuration = 20;
        uint32 skirmishBotsPerSide = 8;
        bool defenceEnabled = true;
        uint32 defenceMinAttackers = 3;
        uint32 defenceDefendersPerAttacker = 4;
        uint32 defenceMaxDefenders = 40;
        uint32 defenceCooldown = 10;
        uint32 botsLevelRange = 5;
    };

    Settings settings;

    bool IsHumanPlayer(Player* player)
    {
        return player && player->IsInWorld() && !GET_PLAYERBOT_AI(player) && !player->IsGameMaster() &&
               player->GetSession() && !player->GetSession()->IsBot() && !CoaWorld::IsTestDriver(player);
    }

    void Tell(Player* player, std::string const& text)
    {
        ChatHandler handler(player->GetSession());
        handler.SendSysMessage("|cffff8000[Living World]|r " + text);
        handler.SendNotification(text);
    }

    class PvpManager
    {
    public:
        static PvpManager& Instance()
        {
            static PvpManager instance;
            return instance;
        }

        bool Busy() const { return _skirmish.active || !_defences.empty(); }

        void Update(uint32 diff)
        {
            if (!_scheduled)
            {
                _scheduled = true;
                RescheduleSkirmish();
            }

            _skirmishTimer -= diff;
            if (_skirmishTimer <= 0)
            {
                if (_skirmish.active)
                    EndSkirmish();
                else if (!settings.enabled || !settings.skirmishEnabled || !StartSkirmish(nullptr))
                    _skirmishTimer = int64(5) * MINUTE_MS;
            }

            _checkTimer += diff;
            if (_checkTimer < CHECK_MS)
                return;
            _checkTimer = 0;
            UpdateDefences();
        }

        bool StartSkirmish(Front const* only)
        {
            if (_skirmish.active)
                return false;

            // A front of the level of the real players online, preferring the one most of them fit.
            Front const* best = only;
            if (!best)
            {
                uint32 bestCount = 0;
                std::vector<Front const*> order;
                for (Front const& f : FRONTS)
                    order.push_back(&f);
                std::shuffle(order.begin(), order.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
                for (Front const* f : order)
                {
                    uint32 count = uint32(Audience(*f).size());
                    if (count > bestCount)
                    {
                        best = f;
                        bestCount = count;
                    }
                }
                if (!best)
                    return false;
            }

            Map* map = sMapMgr->CreateBaseMap(best->mapId);
            if (!map)
                return false;
            Position middle;
            if (!MiddleGround(map, *best, middle))
                return false;

            _skirmish.active = true;
            _skirmish.front = best;
            _skirmish.middle = middle;
            _skirmishTimer = int64(std::max<uint32>(1, settings.skirmishDuration)) * MINUTE_MS;

            uint32 got = 0;
            for (TeamId team : { TEAM_ALLIANCE, TEAM_HORDE })
            {
                BorrowRequest request;
                request.mapId = best->mapId;
                request.goal = middle;
                request.rally = team == TEAM_ALLIANCE ? best->alliancePost : best->hordePost;
                request.team = team;
                request.minLevel = best->minLevel;
                request.maxLevel = best->maxLevel;
                request.count = settings.skirmishBotsPerSide;
                request.nearRadius = 400.0f;
                request.teleport = true;
                request.filter = &HasPvpRuleset;
                got += BorrowBots(BOT_OWNER_PVP_SKIRMISH, request);
            }

            if (got)
                for (Player* bot : Borrowed(BOT_OWNER_PVP_SKIRMISH))
                    bot->UpdatePvP(true, true);

            std::string text = Acore::StringFormat("Fighting has broken out {}! The posts are sending their fighters.",
                best->name);
            for (Player* player : Audience(*best))
                Tell(player, WorldLine(player, "sys_skirmish", { { "place", best->name } }, text));
            BorrowedSay(BOT_OWNER_PVP_SKIRMISH, 0, "pvp_skirmish", { { "place", best->name } }, 2);

            LOG_INFO("module", "CoA World: skirmish {} ({} bots)", best->name, got);
            return true;
        }

        void EndSkirmish()
        {
            ReleaseBots(BOT_OWNER_PVP_SKIRMISH, true);
            _skirmish.active = false;
            RescheduleSkirmish();
        }

        void StopAll()
        {
            if (_skirmish.active)
                EndSkirmish();
            for (Defence const& d : _defences)
                ReleaseBots(d.Owner(), true);
            _defences.clear();
        }

        std::string Status() const
        {
            std::string out = _skirmish.active ? Acore::StringFormat("Skirmish {} ({} bots).", _skirmish.front->name,
                BorrowedCount(BOT_OWNER_PVP_SKIRMISH)) : std::string("No skirmish.");
            for (Defence const& d : _defences)
                out += Acore::StringFormat(" {} defended by {} bots.", d.capital->name, BorrowedCount(d.Owner()));
            if (!settings.enabled)
                out += " (switched off)";
            return out;
        }

    private:
        struct Skirmish
        {
            bool active = false;
            Front const* front = nullptr;
            Position middle;
        };

        struct Defence
        {
            Capital const* capital = nullptr;
            uint32 index = 0;
            time_t clearSince = 0;   // 0 while attackers are inside
            uint32 Owner() const { return BOT_OWNER_PVP_DEFENCE_BASE + index; }
        };

        void RescheduleSkirmish()
        {
            uint32 low = std::max<uint32>(1, settings.skirmishIntervalMin);
            uint32 high = std::max(low, settings.skirmishIntervalMax);
            _skirmishTimer = int64(urand(low, high)) * MINUTE_MS;
        }

        static std::vector<Player*> Borrowed(uint32 owner)
        {
            std::vector<Player*> out;
            for (ObjectGuid const& guid : BorrowedBots(owner))
                if (Player* bot = ObjectAccessor::FindPlayer(guid))
                    if (bot->IsInWorld())
                        out.push_back(bot);
            return out;
        }

        // The defenders already in the city follow the attackers.
        static void RetargetDefenders(uint32 owner, WorldPosition const& goal)
        {
            for (Player* bot : Borrowed(owner))
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                    if (!bot->IsInCombat() && bot->GetMapId() == goal.GetMapId())
                        botAI->rpgInfo.ChangeToGoGrind(goal);
        }

        // A spot of open ground near the middle of the two posts.
        static bool MiddleGround(Map* map, Front const& front, Position& out)
        {
            for (float t : { 0.5f, 0.45f, 0.55f, 0.4f, 0.6f, 0.35f, 0.65f })
            {
                float x = front.alliancePost.GetPositionX() + t * (front.hordePost.GetPositionX() - front.alliancePost.GetPositionX());
                float y = front.alliancePost.GetPositionY() + t * (front.hordePost.GetPositionY() - front.alliancePost.GetPositionY());
                float z = map->GetHeight(x, y, MAX_HEIGHT, false);
                if (z <= INVALID_HEIGHT || map->IsInWater(PHASEMASK_NORMAL, x, y, z, 2.0f))
                    continue;
                out = Position(x, y, z + 0.5f, 0.0f);
                return true;
            }
            return false;
        }

        // Real players of the front's level, both sides.
        static std::vector<Player*> Audience(Front const& front)
        {
            std::vector<Player*> out;
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                if (IsHumanPlayer(player) && player->GetLevel() >= front.minLevel && player->GetLevel() <= front.maxLevel + 5)
                    out.push_back(player);
            return out;
        }

        void UpdateDefences()
        {
            time_t now = time(nullptr);
            for (uint32 i = 0; i < CAPITALS.size(); ++i)
            {
                Capital const& capital = CAPITALS[i];
                std::vector<Player*> attackers;
                for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                    if (IsHumanPlayer(player) && player->IsAlive() && player->GetZoneId() == capital.zoneId &&
                        player->GetTeamId() != capital.team)
                        attackers.push_back(player);

                auto itr = std::find_if(_defences.begin(), _defences.end(),
                    [&capital](Defence const& d) { return d.capital == &capital; });

                if (itr == _defences.end())
                {
                    if (!settings.enabled || !settings.defenceEnabled ||
                        attackers.size() < std::max<uint32>(1, settings.defenceMinAttackers) ||
                        _cooldown[i] > now)
                        continue;
                    Defence d;
                    d.capital = &capital;
                    d.index = i;
                    _defences.push_back(d);
                    Rally(_defences.back(), attackers, true);
                    continue;
                }

                if (attackers.empty())
                {
                    if (!itr->clearSince)
                        itr->clearSince = now;
                    else if (now - itr->clearSince >= 2 * MINUTE)
                    {
                        ReleaseBots(itr->Owner(), true);
                        _cooldown[i] = now + settings.defenceCooldown * MINUTE;
                        LOG_INFO("module", "CoA World: {} defenders stand down", capital.name);
                        _defences.erase(itr);
                    }
                    continue;
                }

                itr->clearSince = 0;
                Rally(*itr, attackers, false);
            }
        }

        // More defenders while the attackers grow, all heading for one of them.
        void Rally(Defence& defence, std::vector<Player*> const& attackers, bool first)
        {
            Capital const& capital = *defence.capital;
            Player* target = attackers[urand(0, attackers.size() - 1)];

            std::vector<uint8> levels;
            for (Player* p : attackers)
                levels.push_back(p->GetLevel());
            std::nth_element(levels.begin(), levels.begin() + levels.size() / 2, levels.end());
            uint32 level = levels[levels.size() / 2];

            uint32 want = std::min<uint32>(settings.defenceMaxDefenders,
                uint32(attackers.size()) * settings.defenceDefendersPerAttacker);
            uint32 have = BorrowedCount(defence.Owner());

            // Those already there follow the attackers.
            Position goal = target->GetPosition();
            WorldPosition worldGoal(capital.mapId, goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ());
            if (have < want)
            {
                BorrowRequest request;
                request.mapId = capital.mapId;
                request.goal = goal;
                request.rally = capital.rally;
                request.team = capital.team;
                request.minLevel = uint32(std::max<int32>(10, int32(level) - int32(settings.botsLevelRange)));
                request.maxLevel = level + settings.botsLevelRange;
                request.count = want - have;
                request.nearRadius = 300.0f;
                request.teleport = true;
                BorrowBots(defence.Owner(), request);
            }
            RetargetDefenders(defence.Owner(), worldGoal);

            if (first)
            {
                std::string text = Acore::StringFormat("{} is under attack! Its defenders are rallying.", capital.name);
                for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                    if (IsHumanPlayer(player) && player->GetTeamId() == capital.team)
                        Tell(player, WorldLine(player, "sys_capital", { { "city", capital.name } }, text));
                BorrowedSay(defence.Owner(), 0, "pvp_capital", { { "city", capital.name } }, 2);
                LOG_INFO("module", "CoA World: {} attacked by {} players, {} defenders", capital.name, attackers.size(),
                    BorrowedCount(defence.Owner()));
            }
        }

        Skirmish _skirmish;
        std::vector<Defence> _defences;
        time_t _cooldown[6] = { 0, 0, 0, 0, 0, 0 };
        bool _scheduled = false;
        int64 _skirmishTimer = 0;
        uint32 _checkTimer = 0;
    };

    PvpManager& Pvp() { return PvpManager::Instance(); }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
        settings.skirmishEnabled = sConfigMgr->GetOption<bool>(key("Skirmish.Enable"), true, false);
        settings.skirmishIntervalMin = sConfigMgr->GetOption<uint32>(key("Skirmish.IntervalMin"), 45, false);
        settings.skirmishIntervalMax = sConfigMgr->GetOption<uint32>(key("Skirmish.IntervalMax"), 90, false);
        settings.skirmishDuration = sConfigMgr->GetOption<uint32>(key("Skirmish.DurationMinutes"), 20, false);
        settings.skirmishBotsPerSide = std::min<uint32>(40, sConfigMgr->GetOption<uint32>(key("Skirmish.BotsPerSide"), 8, false));
        settings.defenceEnabled = sConfigMgr->GetOption<bool>(key("CapitalDefence.Enable"), true, false);
        settings.defenceMinAttackers = sConfigMgr->GetOption<uint32>(key("CapitalDefence.MinAttackers"), 3, false);
        settings.defenceDefendersPerAttacker = std::min<uint32>(20, sConfigMgr->GetOption<uint32>(key("CapitalDefence.DefendersPerAttacker"), 4, false));
        settings.defenceMaxDefenders = std::min<uint32>(200, sConfigMgr->GetOption<uint32>(key("CapitalDefence.MaxDefenders"), 40, false));
        settings.defenceCooldown = sConfigMgr->GetOption<uint32>(key("CapitalDefence.CooldownMinutes"), 10, false);
        settings.botsLevelRange = sConfigMgr->GetOption<uint32>(key("BotsLevelRange"), 5, false);
    }
}

class CoaWorldPvpWorldScript : public WorldScript
{
public:
    CoaWorldPvpWorldScript()
        : WorldScript("CoaWorldPvpWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override { LoadSettings(); }

    void OnUpdate(uint32 diff) override
    {
        if (settings.enabled || Pvp().Busy())
            Pvp().Update(diff);
    }
};

class CoaWorldPvpCommandScript : public CommandScript
{
public:
    CoaWorldPvpCommandScript() : CommandScript("CoaWorldPvpCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable pvpTable = {
            { "status",   HandleStatus,   SEC_GAMEMASTER, Console::Yes },
            { "skirmish", HandleSkirmish, SEC_GAMEMASTER, Console::Yes },
            { "stop",     HandleStop,     SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "pvp", pvpTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(Pvp().Status());
        return true;
    }

    // .coaworld pvp skirmish [1-4]
    static bool HandleSkirmish(ChatHandler* handler, Optional<uint32> front)
    {
        Front const* only = nullptr;
        if (front)
        {
            if (!*front || *front > FRONTS.size())
            {
                handler->PSendSysMessage("Fronts 1 to {}.", FRONTS.size());
                handler->SetSentErrorMessage(true);
                return false;
            }
            only = &FRONTS[*front - 1];
        }
        if (!settings.enabled || !Pvp().StartSkirmish(only))
        {
            handler->SendSysMessage("No skirmish started: switched off, one already running, or no front fits.");
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->SendSysMessage(Pvp().Status());
        return true;
    }

    static bool HandleStop(ChatHandler* handler)
    {
        Pvp().StopAll();
        handler->SendSysMessage(Pvp().Status());
        return true;
    }
};

void AddSC_coa_world_pvp()
{
    new CoaWorldPvpWorldScript();
    new CoaWorldPvpCommandScript();
}
