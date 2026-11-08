/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSoul.h"

#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "NewRpgInfo.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

using namespace CoaSoul;

namespace
{
constexpr uint64 FLUSH_MS = 60 * IN_MILLISECONDS;

std::shared_mutex soulLock;
std::unordered_map<uint32, Soul> souls;  // bot guid (low) -> soul
std::vector<uint32> unsaved;             // drawn, not yet in the table
Weights weights = DefaultWeights();
bool tableExists = false;
uint64 nextFlush = 0;

std::string StatusName(uint32 status)
{
    switch (status)
    {
        case RPG_REST: return "rest";
        case RPG_GO_CITY: return "city";
        case RPG_WANDER_NPC: return "wander_npc";
        case RPG_WANDER_RANDOM: return "wander_random";
        case RPG_TRAVEL_FLIGHT: return "flight";
        case RPG_OUTDOOR_PVP: return "pvp";
        default: return "";
    }
}
}  // namespace

namespace CoaSoul
{
bool Enabled() { return sPlayerbotAIConfig.coaSoulEnable; }

Soul Get(Player* bot)
{
    uint32 const guid = bot ? bot->GetGUID().GetCounter() : 0;
    {
        std::shared_lock<std::shared_mutex> guard(soulLock);
        auto it = souls.find(guid);
        if (it != souls.end())
            return it->second;
    }
    Soul const drawn = Draw(guid, weights);
    std::unique_lock<std::shared_mutex> guard(soulLock);
    auto [it, inserted] = souls.emplace(guid, drawn);
    if (inserted && tableExists)
        unsaved.push_back(guid);
    return it->second;
}

uint32 RpgWeight(Player* bot, uint32 status, int32 configured)
{
    if (configured <= 0)
        return 0;
    if (!Enabled() || !bot || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return uint32(configured);
    std::string const name = StatusName(status);
    if (name.empty())
        return uint32(configured);
    return uint32(configured) * RpgPercent(Get(bot).archetype, name) / 100;
}

void Load()
{
    std::unique_lock<std::shared_mutex> guard(soulLock);
    weights = ParseWeights(sPlayerbotAIConfig.coaSoulWeights);
    souls.clear();
    unsaved.clear();
    tableExists = false;
    // Off: nothing read (a database without the table starts as before).
    if (!sPlayerbotAIConfig.coaSoulEnable)
        return;
    if (!PlayerbotsDatabase.Query("SHOW TABLES LIKE 'playerbots_coa_soul'"))
    {
        LOG_ERROR("server.loading", ">> CoA souls: table playerbots_coa_soul missing (data/sql/playerbots/custom not "
                                    "applied): souls are drawn but not kept.");
        return;
    }
    tableExists = true;
    if (QueryResult r = PlayerbotsDatabase.Query(
            "SELECT bot_guid, archetype, pace, sociability, curiosity, greed, aggression, seed FROM playerbots_coa_soul"))
        do
        {
            Field* f = r->Fetch();
            Soul s;
            s.archetype = std::min<uint8>(f[1].Get<uint8>(), ARCHETYPE_COUNT - 1);
            s.pace = f[2].Get<uint8>();
            s.sociability = f[3].Get<uint8>();
            s.curiosity = f[4].Get<uint8>();
            s.greed = f[5].Get<uint8>();
            s.aggression = f[6].Get<uint8>();
            s.seed = f[7].Get<uint32>();
            souls[f[0].Get<uint32>()] = s;
        } while (r->NextRow());
    LOG_INFO("server.loading", ">> CoA souls: {} kept in the table", souls.size());
}

void Flush(uint64 nowMs)
{
    if (!Enabled() || nowMs < nextFlush)
        return;
    nextFlush = nowMs + FLUSH_MS;
    std::string values;
    {
        std::unique_lock<std::shared_mutex> guard(soulLock);
        if (unsaved.empty())
            return;
        uint32 const now = uint32(GameTime::GetGameTime().count());
        size_t n = 0;
        while (!unsaved.empty() && n < 500)
        {
            uint32 const guid = unsaved.back();
            unsaved.pop_back();
            auto it = souls.find(guid);
            if (it == souls.end())
                continue;
            Soul const& s = it->second;
            values += (n++ ? "," : "") + Acore::StringFormat("({},{},{},{},{},{},{},{},0,1,'',{},{})", guid,
                                                              s.archetype, s.pace, s.sociability, s.curiosity, s.greed,
                                                              s.aggression, s.seed, now, now);
        }
    }
    if (!values.empty())
        PlayerbotsDatabase.Execute("INSERT IGNORE INTO playerbots_coa_soul (bot_guid, archetype, pace, sociability, "
                                   "curiosity, greed, aggression, seed, source, version, persona, created_at, "
                                   "updated_at) VALUES " + values);
}
}  // namespace CoaSoul
