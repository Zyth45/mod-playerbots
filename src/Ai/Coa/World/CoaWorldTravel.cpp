/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: bots that take the zeppelins and the boats.
 *
 * Random bots of the right side walk to a zeppelin tower or a dock and wait for the next departure.
 * When it leaves they "board": they log out on the platform, but only if no real player sees them
 * (none within 150 yards of the bot, none aboard). Otherwise they wait for the next one, and walk
 * away after two missed departures. When the transport docks at the other end, they log back in on
 * the platform there (written into their saved position while away) and walk off; never when a real
 * player stands right on that spot, in which case they come with the next arrival.
 *
 * The transports are the real ones: their docks are the stop frames of their paths, and a run is
 * followed by watching where each transport is. Nothing rides a transport; real passengers aboard
 * when a player is on it are left for after 3.0. Zone changes without a dock (hearthstone) and
 * flights (RPG_TRAVEL_FLIGHT) are not part of this.
 *
 * World thread only. Config: AiPlayerbot.CoaWorld.Travel.*, off by default.
 */

#include "CoaWorldBots.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Transport.h"
#include "TransportMgr.h"
#include "WorldSession.h"

#include <cmath>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    using namespace CoaWorld;

    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Travel.";
    constexpr uint32 TICK_MS = 3 * IN_MILLISECONDS;
    constexpr uint32 RECRUIT_MS = 2 * MINUTE * IN_MILLISECONDS;
    constexpr float DOCKED_RANGE = 8.0f;
    constexpr float AT_PLATFORM = 20.0f;
    constexpr float UNSEEN_RANGE = 150.0f;
    constexpr float ARRIVAL_CLEAR = 15.0f;
    constexpr time_t MAX_TRIP = 30 * MINUTE;

    // Classic transports of CoA (world DB `transports`): entry, side.
    struct TransportDef
    {
        uint32 entry;
        char const* name;
        TeamId team;  // TEAM_NEUTRAL: both
    };

    std::vector<TransportDef> const TRANSPORTS = {
        { 164871, "the zeppelin between Orgrimmar and Undercity", TEAM_HORDE },
        { 175080, "the zeppelin between Orgrimmar and Grom'gol", TEAM_HORDE },
        { 176495, "the zeppelin between Undercity and Grom'gol", TEAM_HORDE },
        { 176231, "the boat between Menethil Harbor and Theramore", TEAM_ALLIANCE },
        { 176310, "the boat between Auberdine and Stormwind Harbor", TEAM_ALLIANCE },
        { 176244, "the boat between Auberdine and Rut'theran Village", TEAM_ALLIANCE },
        { 177233, "the ferry to Feathermoon Stronghold", TEAM_ALLIANCE },
        { 20808, "the boat between Booty Bay and Ratchet", TEAM_NEUTRAL },
    };

    struct Settings
    {
        bool enabled = false;
        uint32 botsPct = 3;
        uint32 botsMax = 20;
        uint32 perPlatform = 3;
        bool hearthstone = true;
    };

    Settings settings;

    struct Dock
    {
        uint32 mapId = 0;
        Position node;      // where the transport stops
        Position platform;  // where passengers stand
        bool valid = false;
    };

    struct Waiter
    {
        ObjectGuid guid;
        uint32 missed = 0;
    };

    struct Traveller
    {
        ObjectGuid guid;
        uint32 toDock = 0;
    };

    struct Route
    {
        TransportDef const* def = nullptr;
        Dock docks[2];
        int32 lastDocked = -1;
        std::vector<Waiter> waiting[2];
        std::vector<Traveller> aboard;

        uint32 Owner(uint32 dock) const { return BOT_OWNER_TRAVEL_BASE + uint32(def - TRANSPORTS.data()) * 2 + dock; }
    };

    bool IsHumanPlayer(Player* player)
    {
        return player && player->IsInWorld() && !GET_PLAYERBOT_AI(player) && player->GetSession() &&
               !player->GetSession()->IsBot();
    }

    bool HumanNear(uint32 mapId, Position const& pos, float range)
    {
        Map* map = sMapMgr->FindBaseMap(mapId);
        if (!map)
            return false;
        for (MapReference const& ref : map->GetPlayers())
            if (Player* player = ref.GetSource())
                if (IsHumanPlayer(player) && player->GetExactDist(&pos) < range)
                    return true;
        return false;
    }

    class TravelManager
    {
    public:
        static TravelManager& Instance()
        {
            static TravelManager instance;
            return instance;
        }

        bool Busy() const
        {
            for (Route const& r : _routes)
                if (!r.aboard.empty() || !r.waiting[0].empty() || !r.waiting[1].empty())
                    return true;
            return !_arrived.empty();
        }

        void Init()
        {
            if (_initialized)
                return;
            _initialized = true;

            for (TransportDef const& def : TRANSPORTS)
            {
                TransportTemplate const* tInfo = sTransportMgr->GetTransportTemplate(def.entry);
                if (!tInfo)
                    continue;

                Route route;
                route.def = &def;
                uint32 found = 0;
                for (KeyFrame const& frame : tInfo->keyFrames)
                {
                    if (!frame.IsStopFrame() || found >= 2)
                        continue;
                    Position node(frame.Node->x, frame.Node->y, frame.Node->z, 0.0f);
                    if (found == 1 && route.docks[0].mapId == frame.Node->mapid && route.docks[0].node.GetExactDist(&node) < 50.0f)
                        continue;
                    Dock& dock = route.docks[found];
                    dock.mapId = frame.Node->mapid;
                    dock.node = node;
                    dock.valid = FindPlatform(dock);
                    ++found;
                }
                if (found == 2 && route.docks[0].valid && route.docks[1].valid)
                    _routes.push_back(std::move(route));
                else
                    LOG_INFO("module", "CoA World: travel leaves out {} (no two docks with a platform)", def.name);
            }
            LOG_INFO("module", "CoA World: travel knows {} transport routes", _routes.size());
        }

        void Update(uint32 diff)
        {
            _tick += diff;
            if (_tick < TICK_MS)
                return;
            uint32 elapsed = _tick;
            _tick = 0;

            time_t now = time(nullptr);
            for (Route& route : _routes)
                UpdateRoute(route, now);

            UpdateArrived();

            _recruit += elapsed;
            if (_recruit >= RECRUIT_MS && settings.enabled)
            {
                _recruit = 0;
                Recruit();
            }
        }

        std::string Status() const
        {
            uint32 waiting = 0, aboard = 0;
            for (Route const& r : _routes)
            {
                waiting += r.waiting[0].size() + r.waiting[1].size();
                aboard += r.aboard.size();
            }
            return Acore::StringFormat("{} transport routes; {} bots waiting on a platform, {} aboard; {} trips made{}.",
                _routes.size(), waiting, aboard, _trips, settings.enabled ? "" : " (switched off)");
        }

    private:
        // The platform: where the nearest standing creature of the dock is (zeppelin master, dock hand).
        static bool FindPlatform(Dock& dock)
        {
            float best = 70.0f;
            for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
            {
                if (data.mapid != dock.mapId)
                    continue;
                Position pos(data.posX, data.posY, data.posZ, data.orientation);
                float dist = dock.node.GetExactDist(&pos);
                if (dist < 6.0f || dist >= best || std::fabs(data.posZ - dock.node.GetPositionZ()) > 25.0f)
                    continue;
                CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(data.id);
                if (!cInfo || cInfo->type != CREATURE_TYPE_HUMANOID)
                    continue;
                best = dist;
                dock.platform = pos;
            }
            return best < 70.0f;
        }

        static Transport* FindTransport(uint32 entry)
        {
            for (uint32 mapId : { 0u, 1u })
                if (Map* map = sMapMgr->FindBaseMap(mapId))
                    for (Transport* t : map->GetAllTransports())
                        if (t && t->GetEntry() == entry)
                            return t;
            return nullptr;
        }

        static bool HumanAboard(Transport* t)
        {
            for (WorldObject* passenger : t->GetPassengers())
                if (Player* player = passenger ? passenger->ToPlayer() : nullptr)
                    if (IsHumanPlayer(player))
                        return true;
            return false;
        }

        void UpdateRoute(Route& route, time_t now)
        {
            Transport* t = FindTransport(route.def->entry);
            if (!t)
                return;

            int32 docked = -1;
            for (int32 i = 0; i < 2; ++i)
                if (t->GetMapId() == route.docks[i].mapId && t->GetExactDist2d(&route.docks[i].node) < DOCKED_RANGE)
                    docked = i;

            if (docked >= 0 && route.lastDocked != docked)
                Arrive(route, uint32(docked), now);
            else if (docked < 0 && route.lastDocked >= 0)
                Depart(route, uint32(route.lastDocked), t, now);

            route.lastDocked = docked;
        }

        // Leaving: the bots on the platform board, unseen; the others wait for the next one.
        void Depart(Route& route, uint32 from, Transport* t, time_t now)
        {
            Dock const& dock = route.docks[from];
            bool humanAboard = HumanAboard(t);
            std::vector<Waiter> still;
            for (Waiter& w : route.waiting[from])
            {
                Player* bot = ObjectAccessor::FindPlayer(w.guid);
                if (!bot || !bot->IsInWorld())
                    continue;  // gone: drop it
                bool onPlatform = bot->GetMapId() == dock.mapId && bot->GetExactDist(&dock.platform) < AT_PLATFORM;
                PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
                bool seen = humanAboard || !botAI || botAI->HasPlayerNearby(UNSEEN_RANGE);
                if (onPlatform && !seen && bot->IsAlive() && !bot->IsInCombat())
                {
                    ForgetBot(route.Owner(from), w.guid);
                    HoldBot(w.guid.GetCounter(), now + MAX_TRIP);
                    sRandomPlayerbotMgr.LogoutPlayerBot(w.guid);
                    route.aboard.push_back({ w.guid, 1 - from });
                    continue;
                }
                if (++w.missed >= 2)
                {
                    if (botAI)
                        botAI->rpgInfo.ChangeToIdle();
                    ForgetBot(route.Owner(from), w.guid);
                    continue;
                }
                still.push_back(w);
            }
            route.waiting[from] = still;
        }

        // Docking: those aboard for this end step off, unless a real player stands right there.
        void Arrive(Route& route, uint32 at, time_t now)
        {
            Dock const& dock = route.docks[at];
            if (HumanNear(dock.mapId, dock.platform, ARRIVAL_CLEAR))
            {
                // Another round trip: they stay aboard a little longer.
                for (Traveller const& tr : route.aboard)
                    if (tr.toDock == at)
                        HoldBot(tr.guid.GetCounter(), now + MAX_TRIP);
                return;
            }

            Map* map = sMapMgr->FindBaseMap(dock.mapId);
            uint32 zone = map ? map->GetZoneId(PHASEMASK_NORMAL, dock.platform.GetPositionX(), dock.platform.GetPositionY(),
                dock.platform.GetPositionZ()) : 0;
            std::vector<Traveller> still;
            for (Traveller const& tr : route.aboard)
            {
                if (tr.toDock != at)
                {
                    still.push_back(tr);
                    continue;
                }
                // Logged back in on the platform, a step apart from each other.
                float angle = frand(0.0f, 2.0f * float(M_PI));
                Player::SavePositionInDB(dock.mapId, dock.platform.GetPositionX() + 2.0f * std::cos(angle),
                    dock.platform.GetPositionY() + 2.0f * std::sin(angle), dock.platform.GetPositionZ(), angle, zone, tr.guid);
                ReleaseHold(tr.guid.GetCounter());
                sRandomPlayerbotMgr.AddPlayerBot(tr.guid, 0);
                _arrived.push_back(tr.guid);
                ++_trips;
            }
            route.aboard = still;
        }

        // Once back in the world, they walk off to whatever they do next.
        void UpdateArrived()
        {
            std::vector<ObjectGuid> still;
            for (ObjectGuid const& guid : _arrived)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                if (!bot)
                {
                    if (++_arrivedWait[guid] < 40)  // about two minutes to log in
                        still.push_back(guid);
                    else
                        _arrivedWait.erase(guid);
                    continue;
                }
                _arrivedWait.erase(guid);
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                    botAI->rpgInfo.ChangeToIdle();
                BotLine(bot, "travel_arrive", {});
            }
            _arrived = still;
        }

        // A few bots of the side, near a platform, go and wait there.
        void Recruit()
        {
            uint32 busy = 0;
            for (Route const& r : _routes)
                busy += r.waiting[0].size() + r.waiting[1].size() + r.aboard.size();
            uint32 budget = BotBudget(settings.botsPct, settings.botsMax);
            if (busy >= budget || _routes.empty())
                return;

            Route& route = _routes[urand(0, _routes.size() - 1)];
            uint32 from = urand(0, 1);
            if (route.waiting[from].size() >= settings.perPlatform)
                return;

            Dock const& dock = route.docks[from];
            BorrowRequest request;
            request.mapId = dock.mapId;
            request.goal = dock.platform;
            request.rally = dock.platform;
            request.team = route.def->team;
            request.balanceTeams = route.def->team == TEAM_NEUTRAL;
            request.minLevel = 10;
            request.count = std::min<uint32>(settings.perPlatform - route.waiting[from].size(), budget - busy);
            request.nearRadius = 600.0f;
            request.teleport = false;  // only bots already in the area walk to a platform

            uint32 before = BorrowedCount(route.Owner(from));
            BorrowBots(route.Owner(from), request);
            std::vector<ObjectGuid> bots = BorrowedBots(route.Owner(from));
            for (size_t i = before; i < bots.size(); ++i)
                route.waiting[from].push_back({ bots[i], 0 });
            BorrowedSay(route.Owner(from), before, "travel_wait", { { "place", route.def->name } }, 1);
        }

        bool _initialized = false;
        std::vector<Route> _routes;
        std::vector<ObjectGuid> _arrived;
        std::unordered_map<ObjectGuid, uint32> _arrivedWait;
        uint32 _tick = 0;
        uint32 _recruit = 0;
        uint32 _trips = 0;
    };

    TravelManager& Travel() { return TravelManager::Instance(); }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), false, false);
        settings.botsPct = std::min<uint32>(50, sConfigMgr->GetOption<uint32>(key("BotsPct"), 3, false));
        settings.botsMax = std::min<uint32>(200, sConfigMgr->GetOption<uint32>(key("BotsMax"), 20, false));
        settings.perPlatform = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("BotsPerPlatform"), 3, false), 1, 10);
        settings.hearthstone = sConfigMgr->GetOption<bool>(key("Hearthstone"), true, false);
    }
}

bool CoaWorldHearthInstead(Player* bot)
{
    if (!settings.enabled || !settings.hearthstone || !bot)
        return false;
    if (!bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() || bot->IsNonMeleeSpellCast(false) ||
        bot->GetMap()->Instanceable())
        return false;

    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
    bot->GetMotionMaster()->Clear();
    bot->StopMoving();
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        botAI->rpgInfo.ChangeToRest();
    // Its home was just set to where it goes: the real hearthstone takes it there, cast for all to see.
    bot->CastSpell(bot, 8690, TriggerCastFlags(TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD |
        TRIGGERED_IGNORE_POWER_AND_REAGENT_COST | TRIGGERED_IGNORE_CAST_ITEM));
    return true;
}

class CoaWorldTravelWorldScript : public WorldScript
{
public:
    CoaWorldTravelWorldScript()
        : WorldScript("CoaWorldTravelWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override
    {
        LoadSettings();
        Travel().Init();
    }

    void OnUpdate(uint32 diff) override
    {
        // Switched off with bots aboard: they still come to their stop.
        if (settings.enabled || Travel().Busy())
            Travel().Update(diff);
    }
};

class CoaWorldTravelCommandScript : public CommandScript
{
public:
    CoaWorldTravelCommandScript() : CommandScript("CoaWorldTravelCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable travelTable = {
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "travel", travelTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(Travel().Status());
        return true;
    }
};

void AddSC_coa_world_travel()
{
    new CoaWorldTravelWorldScript();
    new CoaWorldTravelCommandScript();
}
