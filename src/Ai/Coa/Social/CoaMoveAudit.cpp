/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaMoveAudit.h"

#include "CoaMoveAuditRules.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Engine.h"
#include "GameTime.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "Log.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <vector>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>

using namespace CoaMoveAuditRules;

namespace
{
std::atomic<bool> enabled{false};
uint32 sampleMs = 300;
uint32 throttleMs = 10000;
uint32 npcThrottleMs = 120000;
float npcHomeDist = 15.0f;
float npcMaxScale = 2.5f;
std::string fileName = "coa_move_audit.jsonl";

struct LastMove
{
    std::string action;
    std::string origin;  // 3.2.2: core path, straight line, or a NoteOrigin
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32 pathType = 0;
    uint32 points = 0;
    uint64 atMs = 0;
};

struct AuditSample
{
    uint32 mapId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool onGround = false;
    uint64 atMs = 0;
    std::unordered_map<std::string, uint64> lastLine;  // kind -> when written
    LastMove move;
    // 3.2 "pingpong": the current window (where it began, the furthest from there, the ground walked).
    uint64 pingStartMs = 0;
    uint32 pingMapId = 0;
    float pingX = 0.0f, pingY = 0.0f, pingSpan = 0.0f, pingTravel = 0.0f;
    bool pingSpoiled = false;  // a fight, a flight, water... in the window: not judged
    uint64 teleportMs = 0;     // 3.2.2: the server's last teleport of the bot (NoteTeleport)
};

std::mutex stateLock;
std::unordered_map<uint64, AuditSample> states;
std::unordered_map<uint64, uint64> npcLines;  // creature guid -> when written

std::mutex fileLock;
std::ofstream file;

// 3.2.2: the hour's lines per kind, and a few samples of each, for the hourly summary in the log.
std::mutex summaryLock;
std::map<std::string, uint32> hourCounts;
std::map<std::string, std::vector<std::string>> hourSamples;
std::map<std::string, std::map<std::string, uint32>> hourZones;   // 3.2.2: kind -> zone -> lines
std::map<std::string, std::map<std::string, uint32>> hourCauses;  // 3.2.2: kind -> cause -> lines

// 3.2.2 (Pilote: the audit must not go mute): the samples each exclusion rule kept out of a kind this hour, and the
// lines no move noted explains.
enum Excluded : uint32
{
    EX_TELEPORT_EXEMPT,  // teleport: in flight, on a transport, in water...
    EX_FAST_COMBAT,      // fast: in a fight (a charge)
    EX_WALL_FAR,         // wall: walked further than WALL_MAX_FLAT
    EX_NOT_COMPARED,     // every kind between two samples: across a server teleport or a gap
    EX_UNDER_STOREY,     // underfloor: the floor above is the next storey
    EX_UNDER_MODEL,      // underfloor: standing on a model's floor
    EX_COUNT
};
char const* const excludedNames[EX_COUNT] = {"teleport in flight/transport", "fast in a fight", "wall over 8 yd",
                                             "not compared (server teleport, gap)", "underfloor under a storey",
                                             "underfloor on a model floor"};
std::atomic<uint32> hourExcluded[EX_COUNT];
std::atomic<uint32> hourUnexplained{0};
std::atomic<uint64> nextSummaryMs{0};

// The TOP_N most frequent of a count, "name n, name n".
std::string Top(std::map<std::string, uint32> const& counts)
{
    std::vector<std::pair<uint32, std::string>> sorted;
    for (auto const& [name, n] : counts)
        sorted.emplace_back(n, name);
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    std::string text;
    for (size_t i = 0; i < sorted.size() && i < CoaMoveAuditRules::SUMMARY_TOP; ++i)
        text += (text.empty() ? "" : ", ") + (sorted[i].second.empty() ? std::string("?") : sorted[i].second) + " " +
                std::to_string(sorted[i].first);
    return text;
}

void NoteSummary(std::string const& kind, std::string const& sample, std::string const& zone, std::string const& cause)
{
    std::lock_guard<std::mutex> guard(summaryLock);
    ++hourCounts[kind];
    ++hourZones[kind][zone];
    ++hourCauses[kind][cause];
    if (cause.rfind("no move", 0) == 0)
        ++hourUnexplained;
    std::vector<std::string>& samples = hourSamples[kind];
    if (samples.size() < CoaMoveAuditRules::SUMMARY_SAMPLES)
        samples.push_back(sample);
}

void LogSummaryWhenDue(uint64 nowMs)
{
    uint64 due = nextSummaryMs.load(std::memory_order_relaxed);
    if (!due)
    {
        nextSummaryMs.compare_exchange_strong(due, nowMs + HOUR * IN_MILLISECONDS);
        return;
    }
    if (nowMs < due || !nextSummaryMs.compare_exchange_strong(due, nowMs + HOUR * IN_MILLISECONDS))
        return;
    std::map<std::string, uint32> counts;
    std::map<std::string, std::vector<std::string>> samples;
    std::map<std::string, std::map<std::string, uint32>> zones, causes;
    {
        std::lock_guard<std::mutex> guard(summaryLock);
        counts.swap(hourCounts);
        samples.swap(hourSamples);
        zones.swap(hourZones);
        causes.swap(hourCauses);
    }
    std::string text;
    for (auto const& [kind, n] : counts)
    {
        text += (text.empty() ? "" : " / ") + kind + " " + std::to_string(n) + " [zones: " + Top(zones[kind]) +
                "] [causes: " + Top(causes[kind]) + "]:";
        for (std::string const& sample : samples[kind])
            text += " " + sample + ";";
    }
    std::string excluded;
    for (uint32 i = 0; i < EX_COUNT; ++i)
        excluded += (i ? ", " : "") + std::string(excludedNames[i]) + " " + std::to_string(hourExcluded[i].exchange(0));
    LOG_INFO("playerbots.coa", "move audit (last hour): {} || unexplained (no move noted) {} || excluded: {}",
             text.empty() ? "nothing odd" : text, hourUnexplained.exchange(0), excluded);
}

void Write(std::string const& line)
{
    std::lock_guard<std::mutex> guard(fileLock);
    if (!file.is_open())
        file.open(fileName, std::ios::app);
    if (file.is_open())
    {
        file << line << '\n';
        file.flush();
    }
}

std::string Num(float v)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << v;
    return out.str();
}

char const* GeneratorName(uint32 type)
{
    switch (type)
    {
        case IDLE_MOTION_TYPE: return "IDLE";
        case RANDOM_MOTION_TYPE: return "RANDOM";
        case WAYPOINT_MOTION_TYPE: return "WAYPOINT";
        case CONFUSED_MOTION_TYPE: return "CONFUSED";
        case CHASE_MOTION_TYPE: return "CHASE";
        case HOME_MOTION_TYPE: return "HOME";
        case FLIGHT_MOTION_TYPE: return "FLIGHT";
        case POINT_MOTION_TYPE: return "POINT";
        case FLEEING_MOTION_TYPE: return "FLEEING";
        case DISTRACT_MOTION_TYPE: return "DISTRACT";
        case ASSISTANCE_MOTION_TYPE: return "ASSISTANCE";
        case TIMED_FLEEING_MOTION_TYPE: return "TIMED_FLEEING";
        case FOLLOW_MOTION_TYPE: return "FOLLOW";
        case EFFECT_MOTION_TYPE: return "EFFECT";
        default: return "OTHER";
    }
}

std::string ZoneName(uint32 zoneId)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    return zone ? PlayerbotAI::GetLocalizedAreaName(zone) : std::to_string(zoneId);
}

bool InCapital(uint32 zoneId)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    return zone && (zone->flags & AREA_FLAG_CAPITAL);
}

std::string Time()
{
    time_t const now = GameTime::GetGameTime().count();
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

class CoaMoveAuditNpcScript : public AllCreatureScript
{
public:
    CoaMoveAuditNpcScript() : AllCreatureScript("CoaMoveAuditNpcScript") {}

    // Every creature update: out at once while the audit is off. On: the fixed NPCs of a capital far from home and
    // the oversized creatures of a capital, once per creature and per throttle.
    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (!enabled.load(std::memory_order_relaxed) || !creature || !creature->IsInWorld() || creature->IsPet())
            return;
        uint32 const zoneId = creature->GetZoneId();
        if (!InCapital(zoneId))
            return;
        uint32 const fixedFlags = UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_AUCTIONEER |
                                  UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER;
        float const scale = creature->GetObjectScale();
        bool const big = scale > npcMaxScale;
        bool const away = creature->GetSpawnId() && (creature->GetNpcFlags() & fixedFlags) &&
                          creature->GetDistance(creature->GetHomePosition()) > npcHomeDist;
        if (!big && !away)
            return;
        uint64 const nowMs = GameTime::GetGameTimeMS().count();
        {
            std::lock_guard<std::mutex> guard(stateLock);
            uint64& last = npcLines[creature->GetGUID().GetRawValue()];
            if (!Due(last, nowMs, npcThrottleMs))
                return;
            last = nowMs;
        }
        Unit* victim = creature->GetVictim();
        Position const& home = creature->GetHomePosition();
        std::ostringstream line;
        line << "{\"t\":" << Json(Time()) << ",\"kind\":" << Json(big ? "npc_scale" : "npc_away")
             << ",\"name\":" << Json(creature->GetName()) << ",\"entry\":" << creature->GetEntry()
             << ",\"spawn\":" << creature->GetSpawnId() << ",\"display\":" << creature->GetDisplayId()
             << ",\"scale\":" << Num(scale) << ",\"map\":" << creature->GetMapId() << ",\"zone\":" << Json(ZoneName(zoneId))
             << ",\"x\":" << Num(creature->GetPositionX()) << ",\"y\":" << Num(creature->GetPositionY())
             << ",\"z\":" << Num(creature->GetPositionZ()) << ",\"home\":[" << Num(home.GetPositionX()) << ","
             << Num(home.GetPositionY()) << "," << Num(home.GetPositionZ()) << "]"
             << ",\"gen\":" << Json(GeneratorName(creature->GetMotionMaster()->GetCurrentMovementGeneratorType()))
             << ",\"combat\":" << (creature->IsInCombat() ? "true" : "false")
             << ",\"victim\":" << Json(victim ? victim->GetName() : "") << ",\"faction\":" << creature->GetFaction()
             << ",\"summoner\":" << Json(creature->IsSummon() && creature->GetOwner() ? creature->GetOwner()->GetName() : "")
             << "}";
        Write(line.str());
    }
};
}  // namespace

namespace CoaMoveAudit
{
bool Enabled() { return enabled.load(std::memory_order_relaxed); }

void Load()
{
    sampleMs = std::max<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaMoveAudit.SampleMs", 300));
    throttleMs = 1000 * sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaMoveAudit.ThrottleSeconds", 10);
    fileName = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaMoveAudit.File", "coa_move_audit.jsonl");
    {
        std::lock_guard<std::mutex> guard(fileLock);
        if (file.is_open())
            file.close();
    }
    enabled.store(sConfigMgr->GetOption<bool>("AiPlayerbot.CoaMoveAudit", false), std::memory_order_relaxed);
}

void NoteOrigin(Player* bot, char const* origin)
{
    if (!Enabled() || !bot)
        return;
    std::lock_guard<std::mutex> guard(stateLock);
    LastMove& move = states[bot->GetGUID().GetRawValue()].move;
    move.origin = origin ? origin : "";
    move.atMs = GameTime::GetGameTimeMS().count();
}

void NoteTeleport(Player* bot)
{
    if (!Enabled() || !bot)
        return;
    std::lock_guard<std::mutex> guard(stateLock);
    states[bot->GetGUID().GetRawValue()].teleportMs = GameTime::GetGameTimeMS().count();
}

void NoteMove(Player* bot, char const* action, float x, float y, float z, bool straight)
{
    if (!Enabled() || !bot || !bot->IsInWorld())
        return;
    LastMove move;
    move.action = action ? action : "";
    move.origin = straight ? "straight line" : "core path";
    move.x = x;
    move.y = y;
    move.z = z;
    move.atMs = GameTime::GetGameTimeMS().count();
    PathGenerator path(bot);
    path.CalculatePath(x, y, z, false);
    move.pathType = uint32(path.GetPathType());
    move.points = uint32(path.GetPath().size());
    std::lock_guard<std::mutex> guard(stateLock);
    states[bot->GetGUID().GetRawValue()].move = std::move(move);
}

void Tick(PlayerbotAI* botAI)
{
    if (!Enabled() || !botAI)
        return;
    Player* bot = botAI->GetBot();
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;
    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    LogSummaryWhenDue(nowMs);
    Map* map = bot->GetMap();
    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    bool const exempt = bot->IsFlying() || bot->IsFalling() || bot->GetTransport() || bot->IsInFlight() ||
                        bot->IsInWater() || bot->GetVehicle() || !bot->IsAlive();

    AuditSample before;
    {
        std::lock_guard<std::mutex> guard(stateLock);
        AuditSample& state = states[bot->GetGUID().GetRawValue()];
        if (state.atMs && nowMs < state.atMs + sampleMs)
            return;
        before = state;
    }
    float const floorZ = map->GetHeight(bot->GetPhaseMask(), x, y, z + 0.5f, true, 50.0f);
    float const gridZ = map->GetGridHeight(x, y);
    bool const onGround = !exempt && floorZ > INVALID_HEIGHT && z - floorZ < 0.6f;

    std::vector<std::string> kinds;
    if (InAir(z, floorZ, exempt))
        kinds.push_back("air");
    if (!exempt && onGround && OnRoof(bot->IsOutdoors(), floorZ, gridZ))
        kinds.push_back("roof");
    float flat = 0.0f, dz = 0.0f;
    uint32 dtMs = 0;
    // 3.2.2: not across a teleport by the server, nor across a gap (a logout, an AI asleep): the bot was not walking.
    bool const comparable = before.atMs && before.mapId == bot->GetMapId() && before.teleportMs <= before.atMs &&
                            nowMs <= before.atMs + SAMPLE_GAP_MS;
    if (!comparable && before.atMs)
        ++hourExcluded[EX_NOT_COMPARED];
    if (comparable)
    {
        flat = std::hypot(x - before.x, y - before.y);
        dz = z - before.z;
        dtMs = uint32(nowMs - before.atMs);
        float const speed = std::max(bot->GetSpeed(MOVE_RUN), bot->GetSpeed(MOVE_WALK));
        // 3.2.2 (bench of 5f662017): a taxi flight, a boat is no teleport; a charge in a fight is no "fast" move.
        if (TeleportedFor(flat, dtMs, speed))
        {
            if (!exempt)
                kinds.push_back("teleport");
            else
                ++hourExcluded[EX_TELEPORT_EXEMPT];
        }
        else if (TooFast(flat, dtMs, speed) && !exempt)
        {
            if (!bot->IsInCombat())
                kinds.push_back("fast");
            else
                ++hourExcluded[EX_FAST_COMBAT];
        }
        if (CheckWall(flat) && !exempt && flat > WALL_MAX_FLAT)
            ++hourExcluded[EX_WALL_FAR];
        else if (CheckWall(flat) && !exempt &&
                 !map->isInLineOfSight(before.x, before.y, before.z + 1.5f, x, y, z + 1.5f, bot->GetPhaseMask(),
                                       LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::M2))
            kinds.push_back("wall");
        if (HeightJump(dz, flat, before.onGround && onGround))
            kinds.push_back("zjump");
        // 3.2.2: through the relief, outdoors.
        if (CheckWall(flat) && flat <= TERRAIN_MAX_FLAT && !exempt && bot->IsOutdoors())
        {
            float highest = INVALID_HEIGHT;
            for (uint32 i = 1; i <= TERRAIN_STEPS; ++i)
            {
                float const t = float(i) / float(TERRAIN_STEPS + 1);
                highest = std::max(highest, map->GetGridHeight(before.x + (x - before.x) * t,
                                                               before.y + (y - before.y) * t));
            }
            if (ThroughTerrain(before.z, map->GetGridHeight(before.x, before.y), z, gridZ, highest))
                kinds.push_back("terrain");
        }
    }
    // 3.2.2 (Pilote): under a building's floor in a capital (the vmap's floor read from 6 yards up, over the feet).
    if (onGround && InCapital(bot->GetZoneId()) && (nowMs / sampleMs) % std::max<uint32>(1, INWALL_EVERY_MS / sampleMs) == 0)
    {
        float const above = map->GetHeight(bot->GetPhaseMask(), x, y, z + UNDERFLOOR_MAX, true, UNDERFLOOR_MAX - 0.5f);
        // 3.2.2 (Pilote, bench of 1e991b2d: 473 lines, Ironforge's bank on its floor at 503.94, the terrain at 811): a bot
        // on a model's floor (the vmap alone, read from just over its feet) is on a building's floor, not under one.
        float const model = map->GetMapCollisionData().GetStaticTree().getHeight(x, y, z + 0.5f, 2.0f);
        bool const aboveClose = above > INVALID_HEIGHT && above - z >= UNDERFLOOR_MIN;
        if (aboveClose && OnModelFloor(z, model))
            ++hourExcluded[EX_UNDER_MODEL];
        else if (UnderFloor(z, above))
            kinds.push_back("underfloor");
        else if (aboveClose)
            ++hourExcluded[EX_UNDER_STOREY];
    }
    // 3.2.2: inside a wall or a building's model, now and then.
    if (onGround && (nowMs / sampleMs) % std::max<uint32>(1, INWALL_EVERY_MS / sampleMs) == 0)
    {
        uint32 open = 0;
        for (uint32 i = 0; i < INWALL_DIRECTIONS; ++i)
        {
            float const angle = float(i) * 6.2831853f / float(INWALL_DIRECTIONS);
            if (map->isInLineOfSight(x, y, z + 1.5f, x + INWALL_REACH * std::cos(angle),
                                     y + INWALL_REACH * std::sin(angle), z + 1.5f, bot->GetPhaseMask(),
                                     LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::M2))
                ++open;
        }
        if (InWall(open))
            kinds.push_back("inwall");
    }

    std::vector<std::string> due;
    LastMove move;
    float pingTravel = 0.0f, pingSpan = 0.0f;
    {
        std::lock_guard<std::mutex> guard(stateLock);
        AuditSample& state = states[bot->GetGUID().GetRawValue()];
        // 3.2: back and forth over a window out of combat.
        if (!state.pingStartMs || state.pingMapId != bot->GetMapId() || Teleported(flat))
        {
            state.pingStartMs = nowMs;
            state.pingMapId = bot->GetMapId();
            state.pingX = x;
            state.pingY = y;
            state.pingSpan = state.pingTravel = 0.0f;
            state.pingSpoiled = false;
        }
        else
        {
            state.pingTravel += flat;
            state.pingSpan = std::max(state.pingSpan, std::hypot(x - state.pingX, y - state.pingY));
            state.pingSpoiled |= exempt || bot->IsInCombat();
            if (nowMs >= state.pingStartMs + PING_WINDOW_MS)
            {
                if (!state.pingSpoiled && PingPong(state.pingTravel, state.pingSpan))
                {
                    kinds.push_back("pingpong");
                    pingTravel = state.pingTravel;
                    pingSpan = state.pingSpan;
                }
                state.pingStartMs = nowMs;
                state.pingX = x;
                state.pingY = y;
                state.pingSpan = state.pingTravel = 0.0f;
                state.pingSpoiled = false;
            }
        }
        state.mapId = bot->GetMapId();
        state.x = x;
        state.y = y;
        state.z = z;
        state.onGround = onGround;
        state.atMs = nowMs;
        for (std::string const& kind : kinds)
        {
            uint64& last = state.lastLine[kind];
            if (Due(last, nowMs, throttleMs))
            {
                last = nowMs;
                due.push_back(kind);
            }
        }
        if (!due.empty())
            move = state.move;
    }
    if (due.empty())
        return;

    Engine* engine = botAI->GetEngine(bot->IsInCombat() ? ::BOT_STATE_COMBAT : ::BOT_STATE_NON_COMBAT);
    std::string const lastAction = engine ? engine->GetLastAction() : "";
    std::string const lastRun = engine ? engine->GetLastExecutedAction() : "";
    std::string const rpg = botAI->rpgInfo.ToString();
    for (std::string const& kind : due)
    {
        std::ostringstream line;
        line << "{\"t\":" << Json(Time()) << ",\"kind\":" << Json(kind) << ",\"bot\":" << Json(bot->GetName())
             << ",\"random\":" << (sRandomPlayerbotMgr.IsRandomBot(bot) ? "true" : "false")
             << ",\"level\":" << uint32(bot->GetLevel()) << ",\"map\":" << bot->GetMapId()
             << ",\"zone\":" << Json(ZoneName(bot->GetZoneId())) << ",\"area\":" << bot->GetAreaId()
             << ",\"capital\":" << (InCapital(bot->GetZoneId()) ? "true" : "false") << ",\"from\":[" << Num(before.x)
             << "," << Num(before.y) << "," << Num(before.z) << "],\"to\":[" << Num(x) << "," << Num(y) << ","
             << Num(z) << "],\"flat\":" << Num(flat) << ",\"dz\":" << Num(dz) << ",\"dt\":" << dtMs
             << ",\"floorZ\":" << Num(floorZ) << ",\"gridZ\":" << Num(gridZ)
             << ",\"outdoors\":" << (bot->IsOutdoors() ? "true" : "false")
             << ",\"mounted\":" << (bot->IsMounted() ? "true" : "false")
             << ",\"combat\":" << (bot->IsInCombat() ? "true" : "false") << ",\"speed\":" << Num(bot->GetSpeed(MOVE_RUN))
             << ",\"gen\":" << Json(GeneratorName(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()))
             << ",\"lastAction\":" << Json(lastAction) << ",\"rpg\":" << Json(rpg)
             << ",\"ping\":{\"traveled\":" << Num(pingTravel) << ",\"span\":" << Num(pingSpan) << "}"
             << ",\"move\":{\"action\":" << Json(move.action) << ",\"origin\":" << Json(move.origin) << ",\"to\":[" << Num(move.x) << "," << Num(move.y) << ","
             << Num(move.z) << "],\"path\":" << Json(PathTypeText(move.pathType)) << ",\"points\":" << move.points
             << ",\"ageMs\":" << (move.atMs ? nowMs - move.atMs : 0) << "}}";
        Write(line.str());
        // 3.2.2: the hourly summary's sample: map, where, the action and the move's origin.
        std::ostringstream sample;
        sample << "map " << bot->GetMapId() << " " << Num(x) << " " << Num(y) << " " << Num(z) << " (" << Num(flat)
               << " yd in " << dtMs << " ms, floor " << Num(floorZ) << ", "
               << ZoneName(bot->GetZoneId()) << "), " << (lastAction.empty() ? move.action : lastAction) << ", "
               << (move.origin.empty() ? "no move noted" : move.origin) << " " << PathTypeText(move.pathType)
               // 3.2.2 (Pilote: "air" swimming? flying? a taxi?): the movement flags, moving or not, the generator.
               << ", flags 0x" << std::hex << bot->GetUnitMovementFlags() << std::dec << (bot->isMoving() ? ", moving" : ", still") << ", "
               << GeneratorName(bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
               << (lastRun.empty() ? "" : ", last run ") << lastRun << " -> .go xyz " << Num(x) << " " << Num(y) << " "
               << Num(z + 0.5f) << " " << bot->GetMapId();
        // 3.2.2 (Pilote, bench of 5f662017: "self found equip" first cause of teleports, "add all loot" of air): the
        // cause is what moved the bot, the move noted (its action, its origin) within CAUSE_MS, else "no move" and the
        // generator; the action last run only names what the AI did last, not what moved it.
        std::string const cause = move.atMs && nowMs - move.atMs <= CAUSE_MS
                                      ? (move.action.empty() ? move.origin : move.action + ", " + move.origin)
                                      : "no move, " + std::string(GeneratorName(
                                                          bot->GetMotionMaster()->GetCurrentMovementGeneratorType()));
        // "roof" stays in the file only: the tree of Teldrassil and the ramps of the cities are floors over the terrain.
        if (kind != "roof")
            NoteSummary(kind, sample.str(), ZoneName(bot->GetZoneId()), cause);
    }
}
}  // namespace CoaMoveAudit

void AddSC_coa_move_audit() { new CoaMoveAuditNpcScript(); }
