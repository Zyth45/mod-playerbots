/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: credible levelling routes.
 *
 * When RandomPlayerbotMgr sends a random bot to a place of its level (RandomTeleportForLevel, after a revive or a
 * level-up), any spot with mobs of its level would do: a level 15 Alliance bot could land in the Barrens, a Horde
 * one in Westfall. With the routes on, the spots are first narrowed to the zones its side levels in at its level
 * (CoaWorldZoneLevels.h: Westfall and Loch Modan for the Alliance at 10-20, the Barrens and Silverpine for the Horde,
 * Stranglethorn for both at 30-45...), and most of the time to those on the continent it is on, as a player who
 * goes on to the next zone nearby. No zone of the route has a spot: nothing changes.
 *
 * The zone of each spot is looked up once and kept (thousands of spots, a few bytes each). Called from the map
 * threads: the cache has its own lock.
 *
 * AreaGuard: wherever a bot of the route picks a spot (RandomTeleportForLevel's spots, NewRpg's grind spots and
 * quest places), a subzone whose exploration level (AreaTable) is more than AreaGuardLevels above the bot's is left
 * out: the a47 had level 10 Mulgore bots walk into the Barrens' level 20 packs and die there over and over.
 *
 * Config: AiPlayerbot.CoaWorld.Leveling.*, off by default.
 */

#include "CoaWorldBots.h"
#include "CoaWorldZoneLevels.h"

#include "Config.h"
#include "DBCStores.h"
#include "MapMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <atomic>
#include <shared_mutex>
#include <unordered_map>

namespace
{
    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Leveling.";

    std::atomic<bool> g_enabled{false};
    std::atomic<uint32> g_sameContinentPct{70};
    std::atomic<bool> g_areaGuard{false};
    std::atomic<uint32> g_areaGuardLevels{3};

    std::shared_mutex g_zoneLock;
    std::unordered_map<uint64, uint32> g_zoneOf;

    uint64 SpotKey(WorldLocation const& loc)
    {
        uint64 const x = uint16(int32(loc.GetPositionX()) + 32768);
        uint64 const y = uint16(int32(loc.GetPositionY()) + 32768);
        return (uint64(loc.GetMapId()) << 32) | (x << 16) | y;
    }

    uint32 ZoneOf(WorldLocation const& loc)
    {
        uint64 const key = SpotKey(loc);
        {
            std::shared_lock<std::shared_mutex> lock(g_zoneLock);
            auto itr = g_zoneOf.find(key);
            if (itr != g_zoneOf.end())
                return itr->second;
        }
        uint32 const zone = sMapMgr->GetZoneId(PHASEMASK_NORMAL, loc);
        std::unique_lock<std::shared_mutex> lock(g_zoneLock);
        g_zoneOf[key] = zone;
        return zone;
    }

    // The subzone of a spot, kept as the zones are (the quest places and grind spots come back again and again).
    std::unordered_map<uint64, uint32> g_areaOf;

    uint32 AreaOf(uint32 mapId, float x, float y, float z)
    {
        uint64 const key = (uint64(mapId) << 32) | (uint64(uint16(int32(x) + 32768)) << 16) | uint16(int32(y) + 32768);
        {
            std::shared_lock<std::shared_mutex> lock(g_zoneLock);
            auto itr = g_areaOf.find(key);
            if (itr != g_areaOf.end())
                return itr->second;
        }
        uint32 const area = sMapMgr->GetAreaId(PHASEMASK_NORMAL, mapId, x, y, z);
        std::unique_lock<std::shared_mutex> lock(g_zoneLock);
        g_areaOf[key] = area;
        return area;
    }

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        g_enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
        g_sameContinentPct = std::min<uint32>(100, sConfigMgr->GetOption<uint32>(key("SameContinentPct"), 70, false));
        g_areaGuard = sConfigMgr->GetOption<bool>(key("AreaGuard"), true, false);
        g_areaGuardLevels = std::min<uint32>(20, sConfigMgr->GetOption<uint32>(key("AreaGuardLevels"), 3, false));
    }
}

void CoaWorldLevelingFilter(Player* bot, std::vector<WorldLocation>& locs)
{
    if (!g_enabled.load(std::memory_order_relaxed) || !bot || locs.empty())
        return;

    bool const horde = bot->GetTeamId() == TEAM_HORDE;
    uint8 const level = bot->GetLevel();
    std::vector<WorldLocation> route, sameContinent;
    for (WorldLocation const& loc : locs)
    {
        if (!CoaWorldZones::OnRoute(ZoneOf(loc), level, horde) ||
            CoaWorldAreaTooHigh(bot, loc.GetMapId(), loc.GetPositionX(), loc.GetPositionY(), loc.GetPositionZ()))
            continue;
        route.push_back(loc);
        if (loc.GetMapId() == bot->GetMapId())
            sameContinent.push_back(loc);
    }
    if (route.empty())
        return;
    if (!sameContinent.empty() && urand(1, 100) <= g_sameContinentPct.load(std::memory_order_relaxed))
        locs = std::move(sameContinent);
    else
        locs = std::move(route);
}

bool CoaWorldAreaTooHigh(Player* bot, uint32 mapId, float x, float y, float z)
{
    if (!g_areaGuard.load(std::memory_order_relaxed) || !bot)
        return false;
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(AreaOf(mapId, x, y, z));
    if (!area || area->area_level <= 0)
        return false;
    return area->area_level > int32(bot->GetLevel()) + int32(g_areaGuardLevels.load(std::memory_order_relaxed));
}

class CoaWorldLevelingWorldScript : public WorldScript
{
public:
    CoaWorldLevelingWorldScript()
        : WorldScript("CoaWorldLevelingWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override { LoadSettings(); }
};

void AddSC_coa_world_leveling()
{
    new CoaWorldLevelingWorldScript();
}
