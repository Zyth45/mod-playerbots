/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaWorldFile.h"

#include "CoaLive.h"
#include "CoaSoul.h"
#include "CoaSurnameRules.h"
#include "CoaWorldFileRules.h"
#include "CoaGroupRunsRules.h"
#include "CoaCatchUp.h"
#include "Creature.h"
#include "Group.h"
#include "InstanceScript.h"
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
#include <ctime>
#include <map>
#include <mutex>
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
Ramp ramp;                 // the realm's start (health.ramp), world thread
TickStats ticks;           // the world ticks once the start is over
uint64 nextRampCheck = 0;
std::mutex runsLock;       // the run book: the 10 s pass (world thread) and the kill/death hooks (map threads)
RunBook runs;
uint64 nextRunsPass = 0;

bool DataOn() { return sPlayerbotAIConfig.coaLiveDataFileEnable && sPlayerbotAIConfig.coaLiveDataFiles; }

Kind KindOf(Player* p)
{
    return IsRealPlayer(p) ? Kind::Player : sRandomPlayerbotMgr.IsRandomBot(p) ? Kind::Bot : Kind::Alt;
}

// The groups in a dungeon or a raid now, every 10 s (world thread); the runs gone 5 min end.
void RunsPass(int64 now)
{
    std::map<uint32, Run> seen;  // instance -> what is seen of its group
    {
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld() || player->InBattleground() || player->InArena())
                continue;
            Map* map = player->GetMap();
            Group* group = player->GetGroup();
            if (!map || !group || !map->IsDungeon() || !map->GetInstanceId())
                continue;
            Run& r = seen[map->GetInstanceId()];
            if (!r.instance)
            {
                r.instance = map->GetInstanceId();
                r.map = map->GetId();
                r.name = map->GetMapName();
                r.raid = map->IsRaid();
                r.difficulty = uint32(map->GetDifficulty());
                if (InstanceMap* instance = map->ToInstanceMap())
                    if (InstanceScript* script = instance->GetInstanceScript())
                        r.bossesTotal = int32(script->GetEncounterCount());
                if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
                    r.leader = leader->GetName();
            }
            r.mix.Add(KindOf(player));
        }
    }
    time_t const t = time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    int64 const midnight = now - (local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
    std::lock_guard<std::mutex> guard(runsLock);
    for (auto const& [instance, r] : seen)
        runs.Seen(r, now);
    runs.Close(now, TodaySince(midnight, int64(GameTime::GetStartTime().count())));
}
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
    std::map<std::pair<uint32, uint32>, ZoneCount> zones;
    std::map<std::pair<uint32, uint32>, EveryoneZone> everyoneZones;
    uint32 const surnameMode = sPlayerbotAIConfig.coaBotSurname;
    {
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld())
                continue;
            // "everyone": every character, on this same pass.
            {
                bool const random = sRandomPlayerbotMgr.IsRandomBot(player);
                Kind const kind = IsRealPlayer(player) ? Kind::Player : random ? Kind::Bot : Kind::Alt;
                Map* const map = player->GetMap();
                bool const capital = Capital(player->GetZoneId());
                Place const place = PlaceOf(player->InBattleground(), player->InArena(), map && map->IsRaid(),
                                            map && map->IsDungeon(), capital);
                s.everyone.total.Add(kind);
                s.everyone.places[uint8(place)].Add(kind);
                (player->GetTeamId() == TEAM_ALLIANCE ? s.everyone.alliance : s.everyone.horde).Add(kind);
                EveryoneZone& z = everyoneZones[{player->GetMapId(), player->GetZoneId()}];
                z.capital = capital;
                z.mix.Add(kind);
            }
            if (IsRealPlayer(player))
            {
                ++s.players;
                continue;
            }
            if (!sRandomPlayerbotMgr.IsRandomBot(player))
                continue;
            ++s.online;
            ZoneCount& zone = zones[{player->GetMapId(), player->GetZoneId()}];
            ++zone.bots;
            ++(player->GetTeamId() == TEAM_ALLIANCE ? zone.alliance : zone.horde);

            CoaWorldFile::BotState state;
            state.battleground = player->InBattleground() || player->InArena();
            state.dungeon = !state.battleground && player->GetMap() && player->GetMap()->Instanceable();
            state.fishing = CoaLive::IsFishing(player);
            state.gathering = CoaLive::IsGathering(player);
            state.flying = player->IsInFlight();
            state.inCapital = Capital(player->GetZoneId());
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
                state.rpg = RpgName(botAI->rpgInfo.GetStatus());
            ++s.activities[uint8(Classify(state))];
            if (CoaSoul::Enabled())
                ++s.souls[std::min<uint8>(CoaSoul::Get(player).archetype, 4)];

            if (surnameMode == COA_SURNAME_FAMILY)
            {
                std::string const& name = player->GetName();
                size_t const space = name.find(' ');
                if (space != std::string::npos)
                    ++s.surnames[name.substr(space + 1)];
            }
        }
    }
    for (auto& [key, zone] : zones)
    {
        zone.map = key.first;
        zone.zone = key.second;
        zone.capital = Capital(key.second);
        s.zones.push_back(zone);
    }
    for (auto& [key, z] : everyoneZones)
    {
        z.map = key.first;
        z.zone = key.second;
        s.everyone.zones.push_back(z);
    }
    s.soulsOn = CoaSoul::Enabled();
    s.surnameMode = int32(surnameMode);
    s.surnameListSize = uint32(sPlayerbotAIConfig.coaBotSurnameList.size());
    // Null until the start is over (95 % of the bots online, then 5 min): the start's peaks stay out.
    s.ramp = ramp;
    {
        std::lock_guard<std::mutex> guard(runsLock);
        s.groups = runs.Json();
    }
    s.leveling = CoaCatchUp::Json();
    s.tickAvgMs = ticks.Avg();
    s.tickMaxMs = ticks.Max();
    s.tickP95Ms = ticks.P95();
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
    // The start of the realm, looked at every 10 s until it is over; then each world tick is kept.
    int64 const now = int64(time(nullptr));
    if (ramp.doneAt < 0 && nowMs >= nextRampCheck)
    {
        nextRampCheck = nowMs + 10000;
        uint32 online = 0;
        {
            std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                if (player && player->IsInWorld() && sRandomPlayerbotMgr.IsRandomBot(player))
                    ++online;
        }
        ramp.Update(online, sRandomPlayerbotMgr.GetMaxAllowedBotCount(), now);
    }
    else if (ramp.doneAt >= 0)
        ramp.target = sRandomPlayerbotMgr.GetMaxAllowedBotCount();
    if (ramp.Measuring(now))
        ticks.Add(sWorldUpdateTime.GetLastUpdateTime());
    if (nowMs >= nextRunsPass)
    {
        nextRunsPass = nowMs + 10000;
        RunsPass(now);
    }
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

void NoteBossKill(Player* killer, Creature* boss)
{
    if (!DataOn() || !killer || !boss || !(boss->IsDungeonBoss() || boss->isWorldBoss()))
        return;
    Map* map = killer->GetMap();
    if (!map || !map->IsDungeon())
        return;
    // Cleared: the instance's script holds every encounter done (its last boss down).
    bool cleared = false;
    if (InstanceMap* instance = map->ToInstanceMap())
        if (InstanceScript* script = instance->GetInstanceScript())
        {
            uint32 const total = script->GetEncounterCount();
            uint32 done = 0;
            for (uint32 i = 0; i < total; ++i)
                done += script->IsBossDone(i);
            cleared = total && done == total;
        }
    std::lock_guard<std::mutex> guard(runsLock);
    runs.Kill(map->GetInstanceId(), boss->GetEntry(), cleared, int64(time(nullptr)));
}

void NoteDeath(Player* dead)
{
    if (!DataOn() || !dead)
        return;
    Map* map = dead->GetMap();
    Group* group = dead->GetGroup();
    if (!map || !group || !map->IsDungeon())
        return;
    // A wipe: every member of the group in this instance is dead.
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource(); member && member->IsInWorld() && member->GetMap() == map &&
                                               member->IsAlive())
            return;
    std::lock_guard<std::mutex> guard(runsLock);
    runs.Wipe(map->GetInstanceId());
}
}  // namespace CoaWorldFile
