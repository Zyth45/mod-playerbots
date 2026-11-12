/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: resource rushes ("Gold Rush").
 *
 * The idea comes from mod-gold-rush by Old-Man-Warcraft (GPL-2.0): at random times, rich veins
 * and rare herbs appear in the open world near a real player, the realm is told, and random bots
 * of both sides run there. Rewritten for CoA: the nodes follow the level of the spot (copper to
 * rich thorium, peacebloom to black lotus; all checked against the CoA world DB), a faction zone
 * only draws its own side's bots, and the bots come through the shared CoA World registry
 * (CoaWorldBots), within their own budget.
 *
 * World thread only. Config: AiPlayerbot.CoaWorld.ResourceRush.* (playerbots.conf), off by default.
 */

#include "CoaWorldBots.h"
#include "CoaWorldState.h"
#include "CoaWorldStateRules.h"
#include "CoaWorldAddonRules.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <cmath>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    using namespace CoaWorld;

    constexpr uint32 MINUTE_MS = MINUTE * IN_MILLISECONDS;
    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.ResourceRush.";

    // Nodes by the level of the spot. The last entry of each list is the rare one.
    struct NodeTier
    {
        uint8 minLevel;
        std::vector<uint32> ores;
        std::vector<uint32> herbs;
    };

    std::vector<NodeTier> const TIERS = {
        // Copper Vein; Peacebloom, Silverleaf, Earthroot
        { 1,  { 1731, 1731 },            { 1618, 1617, 1619 } },
        // Tin Vein, Silver Vein; Mageroyal, Briarthorn, Stranglekelp, Bruiseweed
        { 15, { 1732, 1733 },            { 1620, 1621, 2045, 1622 } },
        // Iron Deposit, Gold Vein; Wild Steelbloom, Kingsblood, Liferoot
        { 25, { 1735, 1734 },            { 1623, 1624, 2041 } },
        // Mithril Deposit, Truesilver Deposit; Fadeleaf, Goldthorn, Khadgar's Whisker
        { 35, { 2040, 2047 },            { 2042, 2046, 2043 } },
        // Mithril, Small Thorium, Truesilver; Firebloom, Sungrass, Purple Lotus
        { 45, { 2040, 324, 2047 },       { 2866, 142142, 142140 } },
        // Small Thorium, Rich Thorium, Truesilver; Golden Sansam, Dreamfoil, Mountain Silversage,
        // Plaguebloom, Black Lotus
        { 55, { 324, 175404, 2047 },     { 176583, 176584, 176586, 176587, 176589 } },
    };

    NodeTier const& TierFor(uint8 level)
    {
        NodeTier const* tier = &TIERS.front();
        for (NodeTier const& t : TIERS)
            if (level >= t.minLevel)
                tier = &t;
        return *tier;
    }

    struct Settings
    {
        bool enabled = false;
        bool autoStart = false;  // Auto: rushes come by themselves (else only a GM starts one)
        uint32 intervalMin = 60;
        uint32 intervalMax = 120;
        uint32 duration = 20;
        uint32 retry = 5;
        uint32 minLevel = 10;
        uint32 minNodes = 12;
        uint32 maxNodes = 20;
        float radius = 30.0f;
        uint32 rarePct = 10;
        uint32 announceLevelRange = 10;
        uint32 botsPct = 5;
        uint32 botsMax = 40;
        uint32 botsLevelRange = 5;
        float botsNearRadius = 600.0f;
        bool botsTeleport = true;
        bool botsReturnHome = true;
    };

    Settings settings;

    bool IsHumanPlayer(Player* player)
    {
        return player && player->IsInWorld() && !GET_PLAYERBOT_AI(player) && !player->IsGameMaster() &&
               player->GetSession() && !player->GetSession()->IsBot() && !CoaWorld::IsTestDriver(player);
    }

    void Tell(Player* player, std::string const& text, bool onScreen)
    {
        ChatHandler handler(player->GetSession());
        handler.SendSysMessage("|cffff8000[Living World]|r " + text);
        if (onScreen)
            handler.SendNotification(text);
    }

    std::string AreaName(uint32 id)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(id);
        return area ? std::string(area->area_name[sWorld->GetDefaultDbcLocale()]) : std::string("the wilds");
    }

    bool IsCalmArea(uint32 id)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(id);
        return area && (area->flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SANCTUARY | AREA_FLAG_ARENA));
    }

    class RushManager
    {
    public:
        static RushManager& Instance()
        {
            static RushManager instance;
            return instance;
        }

        void Reschedule()
        {
            uint32 low = std::max<uint32>(1, settings.intervalMin);
            uint32 high = std::max(low, settings.intervalMax);
            _running = false;
            _timer = int64(urand(low, high)) * MINUTE_MS;
        }

        void Update(uint32 diff)
        {
            if (!_scheduled)
            {
                _scheduled = true;
                Reschedule();
            }

            _timer -= diff;
            if (_timer > 0)
                return;

            if (_running)
                Finish();
            else if (!settings.enabled || !settings.autoStart || !Start(nullptr))
                _timer = int64(std::max<uint32>(1, settings.retry)) * MINUTE_MS;
        }

        bool Running() const { return _running; }

        // SQBW WE line of the rush: on (at = end) or end (at = now); empty when it is not for this player.
        std::string AddonLine(Player* player, char const* state) const
        {
            using namespace CoaWorldAddonRules;
            if (!IsHumanPlayer(player) || player->GetLevel() < settings.minLevel ||
                !RushVisible(player->GetLevel(), _level, settings.announceLevelRange))
                return {};
            time_t const now = time(nullptr);
            int64 const at = std::string(state) == "end" ? int64(now) : int64(now) + std::max<int64>(0, _timer) / IN_MILLISECONDS;
            return Cut(std::string("WE\trush\trush:1\t") + state + "\t" + Text(AreaName(_zoneId)) + "\t" +
                       Text(AreaName(_areaId)) + "\t" + std::to_string(at) + "\tGold rush");
        }

        void AddonPush(char const* state) const
        {
            if (CoaWorldAddon::On())
                CoaWorldAddon::Push([&](Player* player) { return AddonLine(player, state); });
        }

        // monde.json (CoaWorldState): the running rush.
        std::string Json(time_t now) const
        {
            if (!_running)
                return "[]";
            int64 const left = std::max<int64>(0, _timer) / IN_MILLISECONDS;
            return "[{\"what\":\"gathering nodes\",\"zone\":" + std::to_string(_zoneId) + ",\"map\":" +
                   std::to_string(_mapId) + ",\"area\":" + CoaWorldStateRules::Quote(AreaName(_areaId)) +
                   ",\"level\":" + std::to_string(_level) + ",\"nodes\":" + std::to_string(_nodes.size()) +
                   ",\"since\":" + std::to_string(int64(now) - (int64(settings.duration) * MINUTE - left)) +
                   ",\"ends\":" + std::to_string(int64(now) + left) + ",\"bots\":" +
                   std::to_string(BorrowedCount(BOT_OWNER_RUSH)) + "}]";
        }

        // gm: a GM start, on the GM's own spot.
        bool Start(Player* gm)
        {
            if (_running)
                return false;

            Player* anchor = gm ? gm : PickAnchor();
            if (!anchor || !PickSpot(anchor))
                return false;

            _level = uint8(std::min<uint32>(anchor->GetLevel(), sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)));
            if (!SpawnNodes())
                return false;

            _running = true;
            _timer = int64(std::max<uint32>(1, settings.duration)) * MINUTE_MS;

            std::string text = Acore::StringFormat(
                "Gold rush! A tremor has laid bare rich veins and rare herbs near {} in {}. Prospectors are already on "
                "their way.", AreaName(_areaId), AreaName(_zoneId));
            for (Player* player : Audience())
                Tell(player, WorldLine(player, "sys_rush_start", { { "subzone", AreaName(_areaId) },
                    { "zone", AreaName(_zoneId) } }, text), true);

            RecruitBots();
            LOG_INFO("module", "CoA World: resource rush near {} in {} (level {}, {} nodes, {} bots)", AreaName(_areaId),
                AreaName(_zoneId), _level, _nodes.size(), BorrowedCount(BOT_OWNER_RUSH));
            LogEvent("rush", "", Acore::StringFormat("gold rush near {} in {} (level {}, {} nodes)", AreaName(_areaId),
                AreaName(_zoneId), _level, _nodes.size()));
            AddonPush("on");
            return true;
        }

        void Finish()
        {
            if (_running)
            {
                AddonPush("end");
                std::string text = Acore::StringFormat("The gold rush near {} is over: the last veins have been picked clean.",
                    AreaName(_areaId));
                for (Player* player : Audience())
                    Tell(player, WorldLine(player, "sys_rush_end", { { "subzone", AreaName(_areaId) },
                        { "zone", AreaName(_zoneId) } }, text), false);
            }

            if (Map* map = sMapMgr->FindBaseMap(_mapId))
                for (ObjectGuid const& guid : _nodes)
                    if (GameObject* go = map->GetGameObject(guid))
                        go->DespawnOrUnsummon();
            _nodes.clear();

            ReleaseBots(BOT_OWNER_RUSH, settings.botsReturnHome);
            LOG_INFO("module", "CoA World: resource rush near {} ended", AreaName(_areaId));
            LogEvent("rush", "", Acore::StringFormat("gold rush near {} ended", AreaName(_areaId)));
            Reschedule();
        }

        std::string Status() const
        {
            if (_running)
                return Acore::StringFormat("Gold rush near {} in {} (level {}): {} nodes, {} bots, {} min left.",
                    AreaName(_areaId), AreaName(_zoneId), _level, _nodes.size(), BorrowedCount(BOT_OWNER_RUSH),
                    (_timer + MINUTE_MS - 1) / MINUTE_MS);
            if (!settings.enabled)
                return "No gold rush now (switched off).";
            if (!settings.autoStart)
                return "No gold rush now. A GM can start one here (.coaworld rush start).";
            return Acore::StringFormat("No gold rush now; next chance in about {} min.", (_timer + MINUTE_MS - 1) / MINUTE_MS);
        }

    private:
        // A real player out in the open world: not in a city, a sanctuary, an instance or on a taxi.
        Player* PickAnchor() const
        {
            std::vector<Player*> anchors;
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                if (!IsHumanPlayer(player) || !player->IsAlive() || player->GetLevel() < settings.minLevel)
                    continue;
                if (player->IsInFlight() || player->InBattleground() || player->InArena() || !player->IsOutdoors())
                    continue;
                Map* map = player->GetMap();
                if (!map || !map->GetEntry()->IsContinent() || IsCalmArea(player->GetZoneId()) || IsCalmArea(player->GetAreaId()))
                    continue;
                anchors.push_back(player);
            }
            return anchors.empty() ? nullptr : anchors[urand(0, anchors.size() - 1)];
        }

        // A spot of open ground 80 to 200 yards away, in the same zone, out of water and towns.
        bool PickSpot(Player* anchor)
        {
            Map* map = anchor->GetMap();
            for (uint32 attempt = 0; attempt < 16; ++attempt)
            {
                float angle = frand(0.0f, 2.0f * float(M_PI));
                float dist = frand(80.0f, 200.0f);
                float x = anchor->GetPositionX() + dist * std::cos(angle);
                float y = anchor->GetPositionY() + dist * std::sin(angle);
                float z = map->GetHeight(x, y, anchor->GetPositionZ() + 30.0f, true, 80.0f);
                if (z <= INVALID_HEIGHT || std::fabs(z - anchor->GetPositionZ()) > 40.0f)
                    continue;
                if (map->IsInWater(PHASEMASK_NORMAL, x, y, z, 2.0f))
                    continue;
                uint32 zone = map->GetZoneId(PHASEMASK_NORMAL, x, y, z);
                uint32 area = map->GetAreaId(PHASEMASK_NORMAL, x, y, z);
                if (zone != anchor->GetZoneId() || IsCalmArea(area))
                    continue;

                _mapId = map->GetId();
                _spot = Position(x, y, z, angle);
                _zoneId = zone;
                _areaId = area;
                return true;
            }
            return false;
        }

        bool SpawnNodes()
        {
            Map* map = sMapMgr->CreateBaseMap(_mapId);
            if (!map)
                return false;
            map->LoadGridsInRange(_spot, settings.radius + 20.0f);

            NodeTier const& tier = TierFor(_level);
            uint32 count = urand(std::min(settings.minNodes, settings.maxNodes), std::max(settings.minNodes, settings.maxNodes));
            uint32 durationSec = std::max<uint32>(1, settings.duration) * MINUTE;
            bool herb = urand(0, 1);
            for (uint32 i = 0; i < count; ++i, herb = !herb)
            {
                std::vector<uint32> const& pool = herb ? tier.herbs : tier.ores;
                // The rare one (last of the list) only now and then.
                uint32 last = pool.size() - 1;
                uint32 entry = roll_chance_i(settings.rarePct) ? pool[last] : pool[urand(0, last ? last - 1 : 0)];

                for (uint32 attempt = 0; attempt < 4; ++attempt)
                {
                    float angle = frand(0.0f, 2.0f * float(M_PI));
                    float dist = frand(2.0f, settings.radius);
                    float x = _spot.GetPositionX() + dist * std::cos(angle);
                    float y = _spot.GetPositionY() + dist * std::sin(angle);
                    float z = map->GetHeight(x, y, _spot.GetPositionZ() + 10.0f);
                    if (z <= INVALID_HEIGHT || std::fabs(z - _spot.GetPositionZ()) > 12.0f ||
                        map->IsInWater(PHASEMASK_NORMAL, x, y, z, 2.0f))
                        continue;

                    if (GameObject* go = map->SummonGameObject(entry, x, y, z, angle, 0.0f, 0.0f, 0.0f, 0.0f, durationSec))
                    {
                        // Gathered once, then gone; kept loaded for the rush even with nobody near.
                        go->SetSpawnedByDefault(false);
                        go->setActive(true);
                        _nodes.push_back(go->GetGUID());
                    }
                    break;
                }
            }
            return !_nodes.empty();
        }

        // Real players of about the spot's level.
        std::vector<Player*> Audience() const
        {
            std::vector<Player*> out;
            int32 low = int32(_level) - int32(settings.announceLevelRange);
            int32 high = int32(_level) + int32(settings.announceLevelRange);
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                if (IsHumanPlayer(player) && player->GetLevel() >= settings.minLevel && int32(player->GetLevel()) >= low &&
                    int32(player->GetLevel()) <= high)
                    out.push_back(player);
            return out;
        }

        // Both sides in a contested zone; a faction zone only draws its own.
        void RecruitBots()
        {
            uint32 budget = BotBudget(settings.botsPct, settings.botsMax);
            if (!budget)
                return;

            BorrowRequest request;
            request.mapId = _mapId;
            request.goal = _spot;
            request.team = TEAM_NEUTRAL;
            request.balanceTeams = true;
            if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(_zoneId))
            {
                if (zone->team == AREATEAM_ALLY)
                    request.team = TEAM_ALLIANCE;
                else if (zone->team == AREATEAM_HORDE)
                    request.team = TEAM_HORDE;
            }

            // Bots from afar arrive out of sight, a little way off the veins.
            if (Map* map = sMapMgr->FindBaseMap(_mapId))
            {
                request.rally = _spot;
                for (uint32 attempt = 0; attempt < 8; ++attempt)
                {
                    float angle = frand(0.0f, 2.0f * float(M_PI));
                    float x = _spot.GetPositionX() + 90.0f * std::cos(angle);
                    float y = _spot.GetPositionY() + 90.0f * std::sin(angle);
                    float z = map->GetHeight(x, y, _spot.GetPositionZ() + 30.0f, true, 80.0f);
                    if (z > INVALID_HEIGHT && std::fabs(z - _spot.GetPositionZ()) < 30.0f &&
                        !map->IsInWater(PHASEMASK_NORMAL, x, y, z, 2.0f))
                    {
                        request.rally = Position(x, y, z + 0.5f, angle + float(M_PI));
                        break;
                    }
                }
            }

            request.minLevel = uint32(std::max<int32>(1, int32(_level) - int32(settings.botsLevelRange)));
            request.maxLevel = _level + settings.botsLevelRange;
            request.count = budget;
            request.nearRadius = settings.botsNearRadius;
            request.teleport = settings.botsTeleport;
            BorrowBots(BOT_OWNER_RUSH, request);
            BorrowedSay(BOT_OWNER_RUSH, 0, "rush_heard", { { "subzone", AreaName(_areaId) }, { "zone", AreaName(_zoneId) } }, 2);
        }

        bool _scheduled = false;
        bool _running = false;
        int64 _timer = 0;
        uint32 _mapId = 0;
        uint32 _zoneId = 0;
        uint32 _areaId = 0;
        uint8 _level = 1;
        Position _spot;
        std::vector<ObjectGuid> _nodes;
    };

    RushManager& Rush() { return RushManager::Instance(); }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
        settings.autoStart = sConfigMgr->GetOption<bool>(key("Auto"), false, false);
        settings.intervalMin = sConfigMgr->GetOption<uint32>(key("IntervalMin"), 60, false);
        settings.intervalMax = sConfigMgr->GetOption<uint32>(key("IntervalMax"), 120, false);
        settings.duration = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("DurationMinutes"), 20, false));
        settings.retry = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("RetryMinutes"), 5, false));
        settings.minLevel = sConfigMgr->GetOption<uint32>(key("MinLevel"), 10, false);
        settings.minNodes = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("MinNodes"), 12, false));
        settings.maxNodes = std::min<uint32>(60, sConfigMgr->GetOption<uint32>(key("MaxNodes"), 20, false));
        settings.radius = float(std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("RadiusYards"), 30, false), 10, 80));
        settings.rarePct = std::min<uint32>(100, sConfigMgr->GetOption<uint32>(key("RarePct"), 10, false));
        settings.announceLevelRange = sConfigMgr->GetOption<uint32>(key("AnnounceLevelRange"), 10, false);
        settings.botsPct = std::min<uint32>(100, sConfigMgr->GetOption<uint32>(key("BotsPct"), 5, false));
        settings.botsMax = sConfigMgr->GetOption<uint32>(key("BotsMax"), 40, false);
        settings.botsLevelRange = sConfigMgr->GetOption<uint32>(key("BotsLevelRange"), 5, false);
        settings.botsNearRadius = float(sConfigMgr->GetOption<uint32>(key("BotsNearRadius"), 600, false));
        settings.botsTeleport = sConfigMgr->GetOption<bool>(key("BotsTeleport"), true, false);
        settings.botsReturnHome = sConfigMgr->GetOption<bool>(key("BotsReturnHome"), true, false);
    }
}

class CoaWorldRushWorldScript : public WorldScript
{
public:
    CoaWorldRushWorldScript()
        : WorldScript("CoaWorldRushWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
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
        // Switched off during a rush: it runs out on its own.
        if (settings.enabled || Rush().Running())
            Rush().Update(diff);
    }
};

class CoaWorldRushCommandScript : public CommandScript
{
public:
    CoaWorldRushCommandScript() : CommandScript("CoaWorldRushCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rushTable = {
            { "status", HandleStatus, SEC_PLAYER,     Console::Yes },
            { "start",  HandleStart,  SEC_GAMEMASTER, Console::No },
            { "stop",   HandleStop,   SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "rush", rushTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(Rush().Status());
        return true;
    }

    static bool HandleStart(ChatHandler* handler)
    {
        if (!settings.enabled)
        {
            handler->SendSysMessage("Resource rushes are switched off (AiPlayerbot.CoaWorld.ResourceRush.Enable).");
            handler->SetSentErrorMessage(true);
            return false;
        }
        Player* gm = handler->GetPlayer();
        if (Rush().Running() || !gm || !gm->GetMap() || !gm->GetMap()->GetEntry()->IsContinent() || !Rush().Start(gm))
        {
            handler->SendSysMessage("No rush started: one is already running, or no open ground was found near you.");
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->SendSysMessage(Rush().Status());
        return true;
    }

    static bool HandleStop(ChatHandler* handler)
    {
        if (Rush().Running())
            Rush().Finish();
        handler->SendSysMessage(Rush().Status());
        return true;
    }
};

std::string CoaWorld::RushesJson(time_t now) { return Rush().Json(now); }

bool CoaWorldAddon::RushOn() { return settings.enabled; }

void CoaWorldAddon::Rushes(Player* player, std::vector<std::string>& out)
{
    if (!Rush().Running())
        return;
    std::string line = Rush().AddonLine(player, "on");
    if (!line.empty())
        out.push_back(std::move(line));
}

void AddSC_coa_world_rush()
{
    new CoaWorldRushWorldScript();
    new CoaWorldRushCommandScript();
}
