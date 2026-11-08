/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaWorldFile.h"

#include "CoaLive.h"
#include "CoaSurnameRules.h"
#include "CoaWorldFileRules.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "NewRpgInfo.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "UpdateTime.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <shared_mutex>
#include <sstream>
#include <thread>

using namespace CoaWorldFile;

namespace
{
constexpr uint64 WATCH_CHECK_MS = 5000;  // the date of watch.json, every 5 s
constexpr uint32 EVERY_S = 10;           // world.json every 10 s while watched

uint64 nextWatchCheck = 0;
uint64 nextBuild = 0;
bool watched = false;
std::atomic<bool> writing{false};

std::filesystem::path Dir()
{
    std::string dir = sPlayerbotAIConfig.coaLiveDataDir;
    if (dir.empty())
        dir = (std::filesystem::path(sConfigMgr->GetOption<std::string>("LogsDir", "", false)) / "squidbot").string();
    return std::filesystem::path(dir);
}

// The dashboard writes squidbot/watch.json every 30 s while a page needs a topic (LIAISON-3.0 §2).
bool WorldWatched()
{
    std::error_code error;
    std::filesystem::path const watch = Dir() / "watch.json";
    if (!std::filesystem::exists(watch, error))
        return false;
    std::ifstream in(watch, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    auto const fileTime = std::filesystem::last_write_time(watch, error);
    int64 const fileEpoch = error ? 0
                                  : std::chrono::duration_cast<std::chrono::seconds>(
                                        (fileTime - std::filesystem::file_time_type::clock::now()) +
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
    return Watched(text.str().substr(0, 4096), fileEpoch, time(nullptr));
}

std::string RpgName(NewRpgStatus status)
{
    switch (status)
    {
        case RPG_REST: return "rest";
        case RPG_GO_GRIND: return "grind";
        case RPG_GO_CAMP: return "camp";
        case RPG_WANDER_RANDOM: return "wander_random";
        case RPG_WANDER_NPC: return "wander_npc";
        case RPG_DO_QUEST: return "quest";
        case RPG_TRAVEL_FLIGHT: return "flight";
        case RPG_OUTDOOR_PVP: return "pvp";
        case RPG_GO_CITY: return "city";
        default: return "idle";
    }
}

bool Capital(uint32 zoneId)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    return zone && (zone->flags & AREA_FLAG_CAPITAL);
}

// One pass over the players of the world (world thread).
Snapshot Build()
{
    Snapshot s;
    std::map<std::pair<uint32, uint32>, uint32> zones;
    uint32 const surnameMode = sPlayerbotAIConfig.coaBotSurname;
    {
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld())
                continue;
            if (IsRealPlayer(player))
            {
                ++s.players;
                continue;
            }
            if (!sRandomPlayerbotMgr.IsRandomBot(player))
                continue;
            ++s.online;
            ++zones[{player->GetMapId(), player->GetZoneId()}];

            CoaWorldFile::BotState state;
            state.battleground = player->InBattleground() || player->InArena();
            state.dungeon = !state.battleground && player->GetMap() && player->GetMap()->Instanceable();
            state.fishing = CoaLive::IsFishing(player);
            state.flying = player->IsInFlight();
            state.inCapital = Capital(player->GetZoneId());
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
                state.rpg = RpgName(botAI->rpgInfo.GetStatus());
            ++s.activities[uint8(Classify(state))];

            if (surnameMode == COA_SURNAME_FAMILY)
            {
                std::string const& name = player->GetName();
                size_t const space = name.find(' ');
                if (space != std::string::npos)
                    ++s.surnames[name.substr(space + 1)];
            }
        }
    }
    for (auto const& [key, bots] : zones)
        s.zones.push_back({key.first, key.second, bots, Capital(key.second)});
    s.surnameMode = int32(surnameMode);
    s.surnameListSize = uint32(sPlayerbotAIConfig.coaBotSurnameList.size());
    s.tickAvgMs = sWorldUpdateTime.GetAverageUpdateTime();
    s.tickMaxMs = sWorldUpdateTime.GetMaxUpdateTime();
    return s;
}

// Off the world thread: a .tmp beside the file, renamed over it (MoveFileEx REPLACE_EXISTING), tried again while a
// reader holds the file. A write still going skips the next one.
void Post(std::filesystem::path const& path, std::string text)
{
    if (writing.exchange(true))
        return;
    std::thread([path, text = std::move(text)]()
    {
        std::filesystem::path const tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << text;
        }
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            std::error_code error;
            std::filesystem::rename(tmp, path, error);
            if (!error)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        writing = false;
    }).detach();
}
}  // namespace

namespace CoaWorldFile
{
void Tick(uint64 nowMs)
{
    if (!sPlayerbotAIConfig.coaLiveDataFileEnable || !sPlayerbotAIConfig.coaLiveDataFiles)
        return;
    if (nowMs >= nextWatchCheck)
    {
        nextWatchCheck = nowMs + WATCH_CHECK_MS;
        watched = WorldWatched();
    }
    if (!watched || nowMs < nextBuild)
        return;
    nextBuild = nowMs + EVERY_S * IN_MILLISECONDS;

    auto const start = std::chrono::steady_clock::now();
    std::string const data = Data(Build());
    int64 const buildUs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    Post(Dir() / "world.json",
         Envelope(time(nullptr), EVERY_S, buildUs, int64(GameTime::GetStartTime().count()), data));
}
}  // namespace CoaWorldFile
