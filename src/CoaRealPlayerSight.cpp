/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaRealPlayerSight.h"

#include "Map.h"
#include "MapReference.h"
#include "Player.h"
#include "Timer.h"
#include "WorldSession.h"

#include <unordered_map>

namespace
{
constexpr uint32 SIGHT_CACHE_MS = 1500;

struct SightEntry
{
    uint32 mapId = 0;
    uint32 instanceId = 0;
    uint32 at = 0;
    bool seen = false;
};

bool ComputeSeen(Player* bot)
{
    Map* map = bot->GetMap();
    if (!map)
        return false;
    for (MapReference const& ref : map->GetPlayers())
    {
        Player* player = ref.GetSource();
        if (!player || player == bot)
            continue;
        WorldSession* session = player->GetSession();
        if (session && !session->IsBot() && player->HaveAtClient(bot))
            return true;
    }
    return false;
}
}  // namespace

bool CoaRealPlayerSight::SeenByRealPlayer(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return false;

    // A bot is updated by the thread of its map, so each thread keeps its own answers.
    thread_local std::unordered_map<ObjectGuid::LowType, SightEntry> cache;
    uint32 const now = getMSTime();
    SightEntry& entry = cache[bot->GetGUID().GetCounter()];
    if (entry.at != 0 && entry.mapId == bot->GetMapId() && entry.instanceId == bot->GetInstanceId() &&
        getMSTimeDiff(entry.at, now) < SIGHT_CACHE_MS)
        return entry.seen;

    entry.mapId = bot->GetMapId();
    entry.instanceId = bot->GetInstanceId();
    entry.at = now ? now : 1;
    entry.seen = ComputeSeen(bot);
    return entry.seen;
}
