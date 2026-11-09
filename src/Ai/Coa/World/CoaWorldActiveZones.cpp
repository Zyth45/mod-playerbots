/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: see CoaWorldActiveZones.h. PlayerbotAI::AllowActive looped over every real player for every bot
// every 5 s (bots x players); with the cache it reads two sets.

#include "CoaWorldActiveZones.h"

#include "CoaWorldActivityRules.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

#include <atomic>
#include <shared_mutex>
#include <unordered_set>

namespace
{
std::shared_mutex g_lock;
std::unordered_set<uint32> g_maps;
std::unordered_set<uint64> g_zones;
std::atomic<bool> g_ready{ false };

constexpr uint32 REFRESH_MS = 10 * IN_MILLISECONDS;

bool Kept() { return sPlayerbotAIConfig.coaActiveZones || sPlayerbotAIConfig.coaIdleBotAI; }

void Refresh()
{
    std::unordered_set<uint32> maps;
    std::unordered_set<uint64> zones;
    for (Player* player : sRandomPlayerbotMgr.GetPlayers())
    {
        if (!player || !player->IsInWorld())
            continue;
        // As AllowActive: an invisible GM wakes no one.
        if (player->IsGameMaster() && !player->IsVisible())
            continue;
        maps.insert(player->GetMapId());
        zones.insert(CoaWorldActivity::ZoneKey(player->GetMapId(), player->GetZoneId()));
    }
    std::unique_lock<std::shared_mutex> lock(g_lock);
    g_maps.swap(maps);
    g_zones.swap(zones);
    g_ready = true;
}

class CoaWorldActiveZonesScript : public WorldScript
{
public:
    CoaWorldActiveZonesScript() : WorldScript("CoaWorldActiveZonesScript", { WORLDHOOK_ON_UPDATE }) { }

    void OnUpdate(uint32 diff) override
    {
        if (!Kept())
        {
            g_ready = false;
            _timer = REFRESH_MS;
            return;
        }
        _timer += diff;
        if (_timer < REFRESH_MS)
            return;
        _timer = 0;
        Refresh();
    }

private:
    uint32 _timer = REFRESH_MS;  // the first update fills it
};
}  // namespace

namespace CoaWorldActiveZones
{
bool Ready() { return g_ready && Kept(); }

bool OnMap(uint32 mapId)
{
    std::shared_lock<std::shared_mutex> lock(g_lock);
    return g_maps.count(mapId) != 0;
}

bool InZone(uint32 mapId, uint32 zoneId)
{
    std::shared_lock<std::shared_mutex> lock(g_lock);
    return g_zones.count(CoaWorldActivity::ZoneKey(mapId, zoneId)) != 0;
}
}  // namespace CoaWorldActiveZones

void AddSC_coa_world_active_zones() { new CoaWorldActiveZonesScript(); }
