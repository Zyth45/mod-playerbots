/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaPathProbe.h"

#include "Chat.h"
#include "Common.h"
#include "CoaCityLifeRules.h"
#include "CoaSafeMoveRules.h"
#include "GameObjectData.h"
#include "Config.h"
#include "CreatureData.h"
#include "DBCStores.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "Log.h"
#include "MMapMgr.h"
#include "Map.h"
#include "MapDefines.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "Timer.h"
#include "WorldSession.h"
#include "Player.h"
#include "Group.h"

#include <algorithm>
#include <set>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <tuple>
#include <mutex>

namespace
{
constexpr int MAX_POLYS = 1024;
constexpr int MAX_POINTS = 256;
constexpr float CITY_RADIUS = 250.0f;
constexpr int CITY_POLYS = 600;
constexpr int CITY_SCAN = 1024;
uint32 checkDelayMs = 0;  // AiPlayerbot.CoaPathCheck: 0 off, else the delay after the start
bool arrivalCheck = true;  // AiPlayerbot.CoaArrivalCheck (the criterion proven by the sonde of a831bd0d)
bool placesOn = true;      // AiPlayerbot.CoaLive.City.Squares
uint32 checkTimer = 0;
bool checkDone = false;

std::string Fmt(char const* format, double a = 0, double b = 0, double c = 0, double d = 0)
{
    char text[200];
    std::snprintf(text, sizeof(text), format, a, b, c, d);
    return text;
}

// As PathGenerator::GetPolyByLocation: a small box first, then a tall one. Recast's axes are WoW's (y, z, x).
dtPolyRef Snap(dtNavMeshQuery const& query, dtQueryFilter const& filter, float const* point, float* nearest)
{
    float extents[3] = {3.0f, 5.0f, 3.0f};
    dtPolyRef ref = 0;
    if (dtStatusSucceed(query.findNearestPoly(point, extents, &filter, &ref, nearest)) && ref)
        return ref;
    extents[1] = 50.0f;
    if (dtStatusSucceed(query.findNearestPoly(point, extents, &filter, &ref, nearest)) && ref)
        return ref;
    return 0;
}

// The end polygon is reached from the start one (a full, not partial, findPath).
bool Joined(dtNavMeshQuery const& query, dtQueryFilter const& filter, dtPolyRef from, float const* fromPt,
            dtPolyRef to, float const* toPt, dtPolyRef* polys, int& count)
{
    count = 0;
    dtStatus const status = query.findPath(from, to, fromPt, toPt, &filter, polys, &count, MAX_POLYS);
    return dtStatusSucceed(status) && !dtStatusDetail(status, DT_PARTIAL_RESULT) && count > 0 &&
           polys[count - 1] == to;
}

struct Query
{
    MMAP::ManagedNavMeshQuery query;
    dtQueryFilter filter;
};

bool Open(uint32 mapId, Query& out)
{
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return false;
    dtNavMesh const* navMesh = map->GetMapCollisionData().GetMMapData().GetNavMesh();
    if (!navMesh)
        return false;
    // A query of our own: the map's one belongs to its thread.
    out.query = MMAP::MMapMgr::CreateNavMeshQuery(const_cast<dtNavMesh*>(navMesh));
    // The bots' filter: ground and liquids, no steep ground.
    out.filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA | NAV_SLIME);
    out.filter.setExcludeFlags(0);
    return out.query != nullptr;
}

// What StandByNpc finds from (fromRef, fromPt) for an NPC at (nx, ny, nz): the direct path's end, then the ring of
// stops at each drop, within the core's reach (InInteractionReach, usual sizes: half a yard short of it) and in
// sight. how: what was found or tried.
// 3.2.2: facing (the NPC's at home; NAN: none) — the stop lies in front of the NPC (CoaSafeMove::InFrontOf).
bool FindStop(Query& q, Map* map, dtPolyRef fromRef, float const* fromPt, float nx, float ny, float nz, float fromX,
              float fromY, std::string* how, float& sx, float& sy, float& sz, float facing = NAN)
{
    auto tryStop = [&](std::string const& what, float tx, float ty, float tz) -> bool
    {
        float const to[3] = {ty, tz, tx};
        float toPt[3];
        dtPolyRef const toRef = Snap(*q.query, q.filter, to, toPt);
        if (!toRef)
            return false;
        dtPolyRef polys[MAX_POLYS];
        int count = 0;
        q.query->findPath(fromRef, toRef, fromPt, toPt, &q.filter, polys, &count, MAX_POLYS);
        float straight[MAX_POINTS * 3];
        int points = 0;
        q.query->findStraightPath(fromPt, toPt, polys, count, straight, nullptr, nullptr, &points, MAX_POINTS);
        if (!points)
            return false;
        float const* end = &straight[(points - 1) * 3];
        float const toNpc = std::sqrt((end[2] - nx) * (end[2] - nx) + (end[0] - ny) * (end[0] - ny) +
                                      (end[1] - nz) * (end[1] - nz));
        auto seen = [&](float up)
        {
            return !map || map->isInLineOfSight(end[2], end[0], end[1] + up, nx, ny, nz + 1.5f, PHASEMASK_NORMAL,
                                                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing);
        };
        bool const seenLow = seen(1.5f);
        bool const sight = CoaSafeMove::SeenOrAtCounter(seenLow, seenLow || seen(CoaSafeMove::STAND_EYES), toNpc,
                                                           end[1] - nz);
        bool const front = std::isnan(facing) || CoaSafeMove::InFrontOf(nx, ny, facing, end[2], end[0]);
        // 3.2.2 (sonde of 603a6549: every auctioneer of Ironforge and Orgrimmar "not joined", bots reaching them in game):
        // the reach with the usual sizes, a humanoid NPC's and a player's combat reach (1.5 each), as ByNpc counts them.
        // 3.2.2: on a floor the server sees (CoaSafeMove::OnServerFloor), as StandByNpc.
        bool const floor = !map || CoaSafeMove::OnServerFloor(end[1], map->GetHeight(PHASEMASK_NORMAL, end[2], end[0],
                                                                                         end[1] + 0.5f));
        bool const ok = CoaSafeMove::InInteractionReach(toNpc, 1.5f, 1.5f) && sight && front && floor;
        if (how && (ok || what == "direct path"))
            *how += (how->empty() ? "" : "; ") + what + Fmt(": end %.2f %.2f %.2f,", end[2], end[0], end[1]) +
                    Fmt(" %.2f yd from the NPC", toNpc) + (seenLow ? "" : sight ? ", at the counter" : ", out of sight") +
                    (front ? "" : ", BEHIND it") + (floor ? "" : ", NO SERVER FLOOR");
        if (ok)
        {
            sx = end[2];
            sy = end[0];
            sz = end[1];
        }
        return ok;
    };
    if (tryStop("direct path", nx, ny, nz))
        return true;
    float const towardStart = std::atan2(fromY - ny, fromX - nx);
    for (uint32 i = 0; i < CoaSafeMove::STAND_RING_TRIES; ++i)
    {
        float radius = 0.0f, angle = 0.0f;
        CoaSafeMove::StandRingPoint(i, towardStart, radius, angle);
        float const px = nx + radius * std::cos(angle), py = ny + radius * std::sin(angle);
        for (uint32 d = 0; d < CoaSafeMove::STAND_RING_DROP_COUNT; ++d)
            if (tryStop(Fmt("ring %.0f (%.1f yd)", i, radius) + Fmt(" %.1f yd lower", CoaSafeMove::STAND_RING_DROPS[d]),
                        px, py, nz - CoaSafeMove::STAND_RING_DROPS[d]))
                return true;
    }
    return false;
}
}  // namespace

namespace CoaPathProbe
{
std::vector<std::string> Probe(uint32_t mapId, float fx, float fy, float fz, float tx, float ty, float tz)
{
    std::vector<std::string> lines;
    Query q;
    if (!Open(mapId, q))
    {
        lines.push_back("no navmesh for map " + std::to_string(mapId));
        return lines;
    }
    dtNavMeshQuery const& query = *q.query;
    float const from[3] = {fy, fz, fx};
    float const to[3] = {ty, tz, tx};
    float fromPt[3], toPt[3];
    dtPolyRef const fromRef = Snap(query, q.filter, from, fromPt);
    dtPolyRef const toRef = Snap(query, q.filter, to, toPt);
    if (!fromRef || !toRef)
    {
        lines.push_back(std::string("no polygon under the ") + (!fromRef ? "start" : "target") +
                        " (its tile not loaded, or off the navmesh)");
        return lines;
    }
    // 3.2.2 (Pilote): the polygons themselves, to see a target taken on another floor.
    lines.push_back(Fmt("start snaps to %.2f %.2f %.2f (%.2f yd off)", fromPt[2], fromPt[0], fromPt[1],
                        std::sqrt(dtVdistSqr(from, fromPt))) + ", polygon " + std::to_string(uint64(fromRef)) +
                    Fmt(" at z %.2f", fromPt[1]));
    // 3.2.2 (Pilote): the city's-floor criterion of the arrivals, shown for both ends, to be proven by the sonde.
    for (auto const& [what, ref, pt] : {std::make_tuple("start", fromRef, fromPt), std::make_tuple("target", toRef, toPt)})
    {
        dtPolyRef around[CITY_SCAN];
        int aroundCount = 0;
        query.findPolysAroundCircle(ref, pt, CITY_RADIUS, &q.filter, around, nullptr, nullptr, &aroundCount, CITY_SCAN);
        lines.push_back(std::string(what) + Fmt(": %.0f polygons joined within %.0f yd", aroundCount, CITY_RADIUS) +
                        (aroundCount >= CITY_SCAN ? " (the scan's cap)" : "") +
                        (aroundCount >= CITY_POLYS ? ", the city's floor" : ", a closed patch"));
    }
    lines.push_back(Fmt("target snaps to %.2f %.2f %.2f (%.2f yd off)", toPt[2], toPt[0], toPt[1],
                        std::sqrt(dtVdistSqr(to, toPt))) + ", polygon " + std::to_string(uint64(toRef)) +
                    Fmt(" at z %.2f", toPt[1]));
    dtPolyRef polys[MAX_POLYS];
    int count = 0;
    bool const joined = Joined(query, q.filter, fromRef, fromPt, toRef, toPt, polys, count);
    float straight[MAX_POINTS * 3];
    int points = 0;
    query.findStraightPath(fromPt, toPt, polys, count, straight, nullptr, nullptr, &points, MAX_POINTS);
    float const* end = points ? &straight[(points - 1) * 3] : fromPt;
    float const endToTarget = std::sqrt((end[2] - tx) * (end[2] - tx) + (end[0] - ty) * (end[0] - ty) +
                                        (end[1] - tz) * (end[1] - tz));
    lines.push_back(std::string(joined ? "same component: the target's polygon is reached"
                                       : "NOT joined: the target's polygon is on another component (or past ") +
                    (joined ? "" : std::to_string(MAX_POLYS) + " polygons)") + Fmt(", %.0f polygons, %.0f points,", count,
                    points) + Fmt(" end %.2f %.2f %.2f, %.2f yd from the target", end[2], end[0], end[1], endToTarget));
    // Every floor within 3 yards of the target and 6 above or below it (Chilton: only his own island is right under
    // him, the city's floor lies 2 to 2.5 yards off), each reached or not, with its nearest point.
    float const box[3] = {3.0f, 6.0f, 3.0f};
    dtPolyRef floors[32];
    int floorCount = 0;
    query.queryPolygons(to, box, &q.filter, floors, &floorCount, 32);
    struct Floor
    {
        float z = 0.0f;
        bool reached = false;
        dtPolyRef ref = 0;
        float x = 0.0f, y = 0.0f, off = 99.0f;
    };
    std::map<int, Floor> heights;  // tenths of a yard -> the floor
    for (int i = 0; i < floorCount; ++i)
    {
        float closest[3];
        bool over = false;
        if (dtStatusFailed(query.closestPointOnPoly(floors[i], to, closest, &over)))
            continue;
        int reached = 0;
        dtPolyRef way[MAX_POLYS];
        bool const in = Joined(query, q.filter, fromRef, fromPt, floors[i], closest, way, reached);
        Floor& floor = heights[int(std::lround(closest[1] * 10.0f))];
        floor.z = closest[1];
        floor.reached = floor.reached || in;
        floor.ref = floor.ref ? floor.ref : floors[i];
        float const off = std::hypot(closest[2] - tx, closest[0] - ty);
        if (off < floor.off)
        {
            floor.off = off;
            floor.x = closest[2];
            floor.y = closest[0];
        }
    }
    std::string floorsText = "floors within 3 yards of the target:";
    for (auto const& [tenth, floor] : heights)
        floorsText += Fmt(" z %.2f at %.2f %.2f", floor.z, floor.x, floor.y) + Fmt(" (%.2f yd off, polygon ", floor.off) +
                      std::to_string(uint64(floor.ref)) + ")" + (floor.reached ? " reached;" : " NOT reached;");
    lines.push_back(heights.empty() ? std::string("no floor within 3 yards of the target") : floorsText);
    return lines;
}

}  // namespace CoaPathProbe

namespace
{
struct Spot
{
    uint32 entry;
    float x, y, z;
    float o = NAN;  // a target's facing at home
};
// (map, capital zone) -> the arrivals and the targets, from the spawns.
using Capitals = std::map<std::pair<uint32, uint32>, std::pair<std::vector<Spot>, std::vector<Spot>>>;

std::atomic<bool> capitalsReady{false};  // 3.2.2: CapitalSpots built (CityUnstick reads it from the map threads)

Capitals const& CapitalSpots()
{
    static Capitals capitals;
    static bool built = false;
    if (built)
        return capitals;
    built = true;
    uint32 const arrivalFlags = UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_FLIGHTMASTER;
    uint32 const targetFlags = UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_FLIGHTMASTER;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        // 3.2.2 (the user: no Dalaran in CoA, level 60 at most): the capitals of the old continents only (Eastern
        // Kingdoms, Kalimdor), where the bots of CoA live.
        if (data.mapid != 0 && data.mapid != 1)
            continue;
        CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(data.id);
        if (!proto || !(proto->npcflag & (arrivalFlags | targetFlags)))
            continue;
        uint32 const zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, data.mapid, data.posX, data.posY, data.posZ);
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
        if (!zone || !(zone->flags & AREA_FLAG_CAPITAL))
            continue;
        auto& lists = capitals[{data.mapid, zoneId}];
        if (proto->npcflag & arrivalFlags)  // TravelMgr's banker point: 6 yards in front, 2 higher
            lists.first.push_back({data.id, data.posX + std::cos(data.orientation) * 6.0f,
                                   data.posY + std::sin(data.orientation) * 6.0f, data.posZ + 2.0f});
        if (proto->npcflag & targetFlags)
            lists.second.push_back({data.id, data.posX, data.posY, data.posZ, data.orientation});
    }
    capitalsReady = true;
    return capitals;
}

// 3.2.2: the arrivals' verdicts (world thread writes, any thread reads), and the points waiting for one.
struct Verdict
{
    CoaPathProbe::Arrival kind = CoaPathProbe::Arrival::Unknown;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool hasPoint = false;  // x, y, z: the city's floor there (in a capital)
    uint32 tries = 0;
};
std::mutex arrivalsLock;
std::map<std::tuple<uint32, int32, int32>, Verdict> arrivals;  // (map, x, y in yards) -> verdict
std::map<std::tuple<uint32, int32, int32>, std::tuple<float, float, float>> pendingArrivals;
uint32 arrivalCounts[4] = {0, 0, 0, 0};  // unknown (tile not loaded), ok, moved, dropped: since the start

std::tuple<uint32, int32, int32> ArrivalKey(uint32 mapId, float x, float y)
{
    return {mapId, int32(std::lround(x)), int32(std::lround(y))};
}

// On the city's floor: the polygons joined to the point's within CITY_RADIUS yards, by the navmesh's links, are many
// (the city: thousands; the closed patches of the Stormwind bank's arrivals and under the counters' NPCs: 2 to 165).
// The sonde of 23/11 found most counter NPCs on such patches, so their positions are no reference. -1: no polygon
// under the point (its tile not loaded).

int OnCityFloor(Query& q, float x, float y, float z)
{
    float const at[3] = {y, z, x};
    float atPt[3];
    dtPolyRef const atRef = Snap(*q.query, q.filter, at, atPt);
    if (!atRef)
        return -1;
    dtPolyRef refs[CITY_SCAN];
    int count = 0;
    q.query->findPolysAroundCircle(atRef, atPt, CITY_RADIUS, &q.filter, refs, nullptr, nullptr, &count, CITY_SCAN);
    return count >= CITY_POLYS ? 1 : 0;
}

// 3.2.2: (x, y, z) on the city's floor (1), moved onto it within 10 yards (2: nx, ny, nz), none there (0), no polygon
// (-1, its tile not loaded).
// 3.2.2 (Pilote): the server sees that floor too: GetHeight from just over it gives it within FLOORS_AGREE. Under the
// Stormwind bank the navmesh has a floor at 99.9 and the vmap none (GetHeight: the terrain, 97.66): a bot put there
// would fall back under the bank at its next ground update (UpdateAllowedPositionZ on its path's points).
constexpr float FLOORS_AGREE = 0.5f;

bool ServerSeesFloor(Map* map, float x, float y, float z)
{
    if (!map)
        return true;
    float const ground = map->GetHeight(PHASEMASK_NORMAL, x, y, z + 0.5f);
    return ground > INVALID_HEIGHT && std::fabs(ground - z) <= FLOORS_AGREE;
}

int RelocateToCity(Query& q, Map* map, float x, float y, float z, float& nx, float& ny, float& nz)
{
    uint32 const mapId = map ? map->GetId() : 0;
    // 3.2.2 (Pilote): a zone whose arrival goes to a spot checked in game (CoaSafeMove::FixedArrival).
    if (CoaSafeMove::FixedArrival(mapId, x, y, z, nx, ny, nz))
    {
        LOG_INFO("playerbots.coa", "fixed arrival: map {} {:.1f} {:.1f} {:.1f} -> {:.1f} {:.1f} {:.1f}", mapId, x, y, z,
                 nx, ny, nz);
        return 2;
    }
    int const here = OnCityFloor(q, x, y, z);
    if (here == 1)
    {
        // 3.2.2 (sonde of 2448cbcb): the height of the navmesh's floor nearest z, when the server sees it too.
        float const at[3] = {y, z, x};
        float on[3];
        Snap(*q.query, q.filter, at, on);
        // 3.2.2 (the user's eye): never on an avoided spot (CoaSafeMove::Avoided).
        if (ServerSeesFloor(map, on[2], on[0], on[1]) && !CoaSafeMove::Avoided(mapId, on[2], on[0], on[1]))
        {
            nx = on[2];
            ny = on[0];
            nz = on[1] + 0.05f;
            return 1;
        }
    }
    if (here == -1)
        return here;
    for (float ring : {2.0f, 4.0f, 6.0f, 8.0f, 10.0f})
        for (uint32 i = 0; i < 8; ++i)
        {
            float const angle = float(i) * 6.2831853f / 8.0f;
            float const probe[3] = {y + ring * std::sin(angle), z, x + ring * std::cos(angle)};
            float snapped[3];
            if (!Snap(*q.query, q.filter, probe, snapped) || std::fabs(snapped[1] - z) > 4.0f)
                continue;
            if (OnCityFloor(q, snapped[2], snapped[0], snapped[1]) == 1 &&
                ServerSeesFloor(map, snapped[2], snapped[0], snapped[1]) &&
                !CoaSafeMove::Avoided(mapId, snapped[2], snapped[0], snapped[1]))
            {
                nx = snapped[2];
                ny = snapped[0];
                nz = snapped[1] + 0.05f;
                return 2;
            }
        }
    return 0;
}

// 3.2.2: an arrival joins its capital when a bot finds a stop by half of its services or more from there.
bool ArrivalJoinsCity(uint32 reached, uint32 targets)
{
    return !targets || reached * 2 >= targets;
}

// How many of the capital's services a bot finds a stop by from (x, y, z).
uint32 StopsFrom(Query& q, Map* map, std::vector<Spot> const& targets, float x, float y, float z)
{
    float const at[3] = {y, z, x};
    float fromPt[3];
    dtPolyRef const fromRef = Snap(*q.query, q.filter, at, fromPt);
    if (!fromRef)
        return 0;
    uint32 reached = 0;
    for (Spot const& to : targets)
    {
        float sx, sy, sz;
        if (FindStop(q, map, fromRef, fromPt, to.x, to.y, to.z, x, y, nullptr, sx, sy, sz, to.o))
            ++reached;
    }
    return reached;
}

// 3.2.2 (Pilote: never drop a capital's last arrival): the most services reached from one of the capital's own
// arrivals (its bankers, innkeepers, flight masters), once per capital. An arrival as good as that one is kept.
std::map<std::pair<uint32, uint32>, uint32> bestStops;

uint32 BestStops(Query& q, Map* map, std::pair<uint32, uint32> const& key,
                 std::pair<std::vector<Spot>, std::vector<Spot>> const& lists)
{
    auto found = bestStops.find(key);
    if (found != bestStops.end())
        return found->second;
    uint32 best = 0;
    for (Spot const& from : lists.first)
        best = std::max(best, StopsFrom(q, map, lists.second, from.x, from.y, from.z));
    bestStops[key] = best;
    return best;
}

void JudgeArrival(std::tuple<uint32, int32, int32> const& key, float x, float y, float z)
{
    uint32 const mapId = std::get<0>(key);
    Verdict verdict;
    uint32 const zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, x, y, z);
    auto capital = CapitalSpots().find({mapId, zoneId});
    Query q;
    if (capital == CapitalSpots().end() || capital->second.second.empty())
        verdict.kind = CoaPathProbe::Arrival::Ok;  // not in a capital
    else if (!Open(mapId, q))
        verdict.kind = CoaPathProbe::Arrival::Ok;  // a map without a navmesh: nothing to judge
    else
    {
        Map* map = sMapMgr->CreateBaseMap(mapId);
        if (map && !map->IsGridLoaded(x, y))
            map->LoadGrid(x, y);  // the world thread: its vmap, for ServerSeesFloor
        int const here = RelocateToCity(q, map, x, y, z, verdict.x, verdict.y, verdict.z);
        verdict.hasPoint = here == 1 || here == 2;
        verdict.kind = here == 1   ? CoaPathProbe::Arrival::Ok
                       : here == 2 ? CoaPathProbe::Arrival::Moved
                       : here == 0 ? CoaPathProbe::Arrival::Dropped
                                   : CoaPathProbe::Arrival::Unknown;
        // 3.2.2 (sonde of 5f662017: Darnassus's arrival 6735 on the city's floor, its paths to the auctioneers' terrace
        // ending 16 yards short, on the floor under it): an arrival from which a bot finds a stop by fewer than half of
        // the capital's services is dropped.
        if (verdict.hasPoint)
        {
            uint32 const targets = uint32(capital->second.second.size());
            uint32 const reached = StopsFrom(q, map, capital->second.second, verdict.x, verdict.y, verdict.z);
            if (!ArrivalJoinsCity(reached, targets) && reached < BestStops(q, map, capital->first, capital->second))
            {
                verdict.kind = CoaPathProbe::Arrival::Dropped;
                verdict.hasPoint = false;
                LOG_INFO("playerbots.coa", "arrival check: map {} {:.1f} {:.1f} {:.1f}: a stop by {} of the city's {} "
                         "services only, dropped", mapId, x, y, z, reached, targets);
            }
        }
    }
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    std::lock_guard<std::mutex> guard(arrivalsLock);
    Verdict& kept = arrivals[key];
    verdict.tries = kept.tries + 1;
    if (verdict.kind == CoaPathProbe::Arrival::Unknown && verdict.tries < 5)
        pendingArrivals.emplace(key, std::make_tuple(x, y, z));  // its tile not loaded: later
    kept = verdict;
    ++arrivalCounts[uint32(verdict.kind)];
    if (verdict.kind == CoaPathProbe::Arrival::Moved || verdict.kind == CoaPathProbe::Arrival::Dropped)
        LOG_INFO("playerbots.coa", "arrival check: map {} {:.1f} {:.1f} {:.1f} ({}) off the city's floor: {}", mapId, x, y,
                 z, zone ? zone->area_name[0] : "?",
                 verdict.kind == CoaPathProbe::Arrival::Moved
                     ? Fmt("moved to %.1f %.1f %.1f", verdict.x, verdict.y, verdict.z)
                     : std::string("dropped (no point of the city's floor within 10 yards)"));
}
}  // namespace

namespace CoaPathProbe
{
Arrival ArrivalVerdict(uint32_t mapId, float& x, float& y, float& z)
{
    if (!arrivalCheck)
        return Arrival::Ok;
    auto const key = ArrivalKey(mapId, x, y);
    std::lock_guard<std::mutex> guard(arrivalsLock);
    auto found = arrivals.find(key);
    if (found == arrivals.end())
    {
        if (pendingArrivals.size() < 4096)
            pendingArrivals.emplace(key, std::make_tuple(x, y, z));
        return Arrival::Unknown;
    }
    if (found->second.hasPoint)
    {
        x = found->second.x;
        y = found->second.y;
        z = found->second.z;
    }
    return found->second.kind;
}

std::vector<std::string> ArrivalsDisagree()
{
    std::vector<std::string> lines;
    uint32 total = 0, apart = 0;
    for (auto const& [key, lists] : CapitalSpots())
    {
        Map* map = sMapMgr->CreateBaseMap(key.first);
        Query q;
        if (!map || !Open(key.first, q))
            continue;
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(key.second);
        std::string const name = zone ? zone->area_name[0] : std::to_string(key.second);
        for (Spot const& given : lists.first)
        {
            if (!map->IsGridLoaded(given.x, given.y))
                map->LoadGrid(given.x, given.y);
            ++total;
            float const at[3] = {given.y, given.z, given.x};
            float on[3];
            if (!Snap(*q.query, q.filter, at, on))
                continue;
            float const server = map->GetHeight(PHASEMASK_NORMAL, given.x, given.y, given.z + 0.5f);
            if (std::fabs(server - on[1]) <= 1.0f)
                continue;
            ++apart;
            lines.push_back(name + Fmt(": arrival of %.0f at %.1f %.1f", given.entry, given.x, given.y) +
                            Fmt(" %.1f: navmesh %.2f, ", given.z, on[1]) + Fmt("GetHeight %.2f (%.2f apart)", server,
                            std::fabs(server - on[1])) + Fmt("  -> .go xyz %.1f %.1f", given.x, given.y) +
                            Fmt(" %.1f %.0f", on[1] + 0.5f, key.first));
        }
    }
    lines.push_back(std::to_string(apart) + " of " + std::to_string(total) + " arrivals with the navmesh and the server's "
                    "floors more than a yard apart");
    return lines;
}

std::vector<std::string> StopProbe(uint32_t mapId, float x, float y, float z, uint32_t entryOrSpawn)
{
    std::vector<std::string> lines;
    CreatureData const* npc = nullptr;
    float best = 1e9f;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (data.mapid != mapId || (data.id != entryOrSpawn && spawnId != entryOrSpawn))
            continue;
        float const d = std::hypot(data.posX - x, data.posY - y);
        if (d < best)
        {
            best = d;
            npc = &data;
        }
    }
    if (!npc)
    {
        lines.push_back("no creature of entry or spawn " + std::to_string(entryOrSpawn) + " on map " + std::to_string(mapId));
        return lines;
    }
    Map* map = sMapMgr->CreateBaseMap(mapId);
    for (auto const& [px, py] : {std::make_pair(x, y), std::make_pair(npc->posX, npc->posY)})
        if (map && !map->IsGridLoaded(px, py))
            map->LoadGrid(px, py);
    Query q;
    if (!Open(mapId, q))
    {
        lines.push_back("no navmesh for map " + std::to_string(mapId));
        return lines;
    }
    float const from[3] = {y, z, x};
    float fromPt[3];
    dtPolyRef const fromRef = Snap(*q.query, q.filter, from, fromPt);
    if (!fromRef)
    {
        lines.push_back("no polygon at the start");
        return lines;
    }
    float const nx = npc->posX, ny = npc->posY, nz = npc->posZ;
    lines.push_back(Fmt("NPC %.0f at %.2f %.2f %.2f", npc->id, nx, ny, nz) + Fmt(", facing %.2f", npc->orientation) +
                    Fmt(", start %.2f %.2f %.2f", fromPt[2],
                    fromPt[0], fromPt[1]));
    std::string how;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    if (FindStop(q, map, fromRef, fromPt, nx, ny, nz, x, y, &how, sx, sy, sz, npc->orientation))
        lines.push_back(how + Fmt(" -> STOP at %.2f %.2f %.2f,", sx, sy, sz) +
                        Fmt(" %.2f yd from the NPC", std::sqrt((sx - nx) * (sx - nx) + (sy - ny) * (sy - ny) +
                                                                 (sz - nz) * (sz - nz))));
    else
        lines.push_back(how + "; no stop: neither the direct path nor the " +
                        std::to_string(CoaSafeMove::STAND_RING_TRIES * CoaSafeMove::STAND_RING_DROP_COUNT) +
                        " ring points end within reach of the NPC in its sight (StandByNpc fails; the island check follows)");
    return lines;
}

std::vector<std::string> CheckCapitals()
{
    Capitals const& capitals = CapitalSpots();
    std::vector<std::string> lines;
    uint32 pairs = 0, cut = 0, unknown = 0;
    for (auto const& [key, lists] : capitals)
    {
        // 3.2.2 (Pilote): the grids of the capital loaded first (their navmesh tiles with them): an unloaded tile
        // judged nothing on the first sonde (332 "not joined" of 654).
        Map* map = sMapMgr->CreateBaseMap(key.first);
        if (map)
            for (auto const* list : {&lists.first, &lists.second})
                for (Spot const& spot : *list)
                    if (!map->IsGridLoaded(spot.x, spot.y))
                        map->LoadGrid(spot.x, spot.y);
        Query q;
        if (!Open(key.first, q))
            continue;
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(key.second);
        std::string const name = zone ? zone->area_name[0] : std::to_string(key.second);
        uint32 joinedHere = 0, cutHere = 0, unknownHere = 0, movedHere = 0, droppedHere = 0;
        for (Spot const& given : lists.first)
        {
            // 3.2.2 (Pilote): the arrival as TravelMgr gives it, where it snaps, its patch, and as CoaArrivalCheck
            // corrects it; the pairs are judged from the corrected one (a dropped arrival is never used).
            float const givenRc[3] = {given.y, given.z, given.x};
            float givenPt[3];
            dtPolyRef const givenRef = Snap(*q.query, q.filter, givenRc, givenPt);
            int polys = 0;
            if (givenRef)
            {
                dtPolyRef around[CITY_SCAN];
                q.query->findPolysAroundCircle(givenRef, givenPt, CITY_RADIUS, &q.filter, around, nullptr, nullptr, &polys,
                                               CITY_SCAN);
            }
            Spot from = given;
            int const verdict = RelocateToCity(q, map, given.x, given.y, given.z, from.x, from.y, from.z);
            float const serverZ = map ? map->GetHeight(PHASEMASK_NORMAL, given.x, given.y, given.z + 0.5f) : 0.0f;
            lines.push_back(name + Fmt(": arrival of %.0f (%.1f %.1f", given.entry, given.x, given.y) +
                            Fmt(" %.1f) snaps at z %.2f, ", given.z, givenRef ? givenPt[1] : 0.0f) +
                            Fmt("GetHeight %.2f, ", serverZ) +
                            Fmt("%.0f polygons joined: ", polys) +
                            (verdict == 1   ? std::string("on the city's floor")
                             : verdict == 2 ? Fmt("moved to %.1f %.1f %.1f", from.x, from.y, from.z)
                             : verdict == 0 ? std::string("DROPPED (no city floor within 10 yards)")
                                            : std::string("no polygon")));
            if (verdict == 2)
                ++movedHere;
            if (verdict == 0)
            {
                ++droppedHere;
                continue;
            }
            // 3.2.2 (Pilote, sonde of 96b4114c: Darnassus's 6735 still judged here, its 4 pairs not joined): the same
            // rule as JudgeArrival, which only runs when a bot is sent there: under half the services, dropped unless
            // it is the capital's best arrival.
            if (verdict == 1 || verdict == 2)
            {
                uint32 const targets = uint32(lists.second.size());
                uint32 const reached = StopsFrom(q, map, lists.second, from.x, from.y, from.z);
                uint32 const best = BestStops(q, map, key, lists);
                if (!ArrivalJoinsCity(reached, targets) && reached < best)
                {
                    ++droppedHere;
                    lines.push_back(name + Fmt(": arrival of %.0f DROPPED: a stop by %.0f", given.entry, reached) +
                                    Fmt(" of the %.0f services (the best arrival: %.0f)", targets, best));
                    continue;
                }
            }
            float const fromRc[3] = {from.y, from.z, from.x};
            float fromPt[3];
            dtPolyRef const fromRef = Snap(*q.query, q.filter, fromRc, fromPt);
            for (Spot const& to : lists.second)
            {
                if (from.entry == to.entry)
                    continue;
                ++pairs;
                float const toRc[3] = {to.y, to.z, to.x};
                float toPt[3];
                dtPolyRef const toRef = Snap(*q.query, q.filter, toRc, toPt);
                if (!fromRef || !toRef)
                {
                    ++unknown;
                    ++unknownHere;
                    continue;  // no polygon there even with its grid loaded: told apart, in the totals
                }
                // 3.2.2: joined when the bot finds a stop by the NPC from there (FindStop: the counters' NPCs stand on
                // closed patches of their own, so their very polygon is no test).
                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                if (FindStop(q, map, fromRef, fromPt, to.x, to.y, to.z, from.x, from.y, nullptr, sx, sy, sz, to.o))
                {
                    ++joinedHere;
                    continue;
                }
                ++cut;
                ++cutHere;
                lines.push_back(name + Fmt(": from the arrival of %.0f (%.1f %.1f", from.entry, from.x, from.y) +
                                Fmt(" %.1f) to %.0f: not joined", from.z, to.entry));
            }
        }
        lines.push_back(name + ": " + std::to_string(joinedHere) + " joined, " + std::to_string(cutHere) +
                        " not joined, " + std::to_string(unknownHere) + " unknown (no polygon at an end); arrivals " +
                        std::to_string(lists.first.size()) + ": " + std::to_string(movedHere) + " moved, " +
                        std::to_string(droppedHere) + " dropped");
    }
    lines.push_back("capitals' path check: " + std::to_string(pairs) + " pairs, " + std::to_string(cut) +
                    " not joined, " + std::to_string(unknown) + " unknown");
    return lines;
}
}  // namespace CoaPathProbe

namespace
{
// 3.2.2: the squares, per capital (world thread writes once, map threads read).
std::mutex placesLock;
std::map<std::pair<uint32, uint32>, std::vector<CoaPathProbe::Place>> places;  // (map, zone) -> its squares
std::set<std::pair<uint32, uint32>> placesTried;  // capitals searched (even with no square found)
std::atomic<uint32> placesTaken{0};
// 3.2.2 (measure only): seats used or refused at arrival, and each holder's latest state (reset at each report).
struct SeatSeen
{
    uint8 state;  // 0 sat on the chair (SIT_*_CHAIR), 1 sat on the ground (SIT), 2 anything else (standing)
    int8 use;     // 1 used, -1 refused, 0 unknown
    std::string name;
    float x, y, z;
    uint32 entry;
};
std::atomic<uint32> seatsUsed{0}, seatsRefused{0}, seatsSatAgain{0};
std::mutex seatsLock;
std::map<uint64, SeatSeen> seatsHeld;
// 3.2.2 (CityUnstick, the user: bots in the Stormwind inn's floor, a bot still for an hour by the inn): random bots of
// a capital in the floor, or still on a wander whose targets no step reaches, put on the city's nearest checked arrival.
struct StillState
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32 lastMs = 0;     // last look (getMSTime)
    uint32 stillMs = 0;    // time still on a wander, the AI awake
    uint32 buriedAt = 0;   // last floor check
    uint32 unstickAt = 0;  // last move out
};
bool cityUnstickOn = true;  // AiPlayerbot.CoaLive.CityUnstick.Enable
bool npcStopAsideOn = true;  // AiPlayerbot.CoaLive.NpcStopAside
uint32 stillMinutes = 25;   // AiPlayerbot.CoaLive.CityUnstick.StillMinutes
std::mutex stillLock;
std::unordered_map<uint64, StillState> stillBots;
std::map<std::string, uint32> stillByZone;  // since the last report
std::vector<std::string> unstickSamples;    // up to 3 since the last report
std::atomic<uint32> unstuckBuried{0}, unstuckStill{0}, unstickCapped{0}, unstickNoArrival{0}, unstickHour{0};
std::atomic<uint32> rescued{0}, rescueSeen{0};  // 3.2.2: RescueFromPatch
uint32 placeIds = 0;

struct Service
{
    uint32 kind;  // a bit: 1 auctioneer, 2 banker, 4 innkeeper, 8 flight master, 16 quest board, 32 mailbox
    float x, y, z;
};

// A capital's services and the ways between them (straight paths of the navmesh).
struct Scene
{
    std::vector<Service> services;
    std::vector<std::array<float, 4>> segments;
    bool anyTile = false;
};

Scene BuildScene(uint32 mapId, uint32 zoneId, Query& q)
{
    Scene scene;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (data.mapid != mapId)
            continue;
        CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(data.id);
        if (!proto)
            continue;
        uint32 kind = 0;
        if (proto->npcflag & UNIT_NPC_FLAG_AUCTIONEER)
            kind = 1;
        else if (proto->npcflag & UNIT_NPC_FLAG_BANKER)
            kind = 2;
        else if (proto->npcflag & UNIT_NPC_FLAG_INNKEEPER)
            kind = 4;
        else if (proto->npcflag & UNIT_NPC_FLAG_FLIGHTMASTER)
            kind = 8;
        if (kind && sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, data.posX, data.posY, data.posZ) == zoneId)
            scene.services.push_back({kind, data.posX, data.posY, data.posZ});
    }
    for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
    {
        if (data.mapid != mapId)
            continue;
        GameObjectTemplate const* proto = sObjectMgr->GetGameObjectTemplate(data.id);
        uint32 const kind = !proto ? 0 : proto->type == GAMEOBJECT_TYPE_QUESTGIVER ? 16
                                        : proto->type == GAMEOBJECT_TYPE_MAILBOX  ? 32 : 0;
        if (kind && sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, data.posX, data.posY, data.posZ) == zoneId)
            scene.services.push_back({kind, data.posX, data.posY, data.posZ});
    }
    for (size_t a = 0; a < scene.services.size(); ++a)
        for (size_t b = a + 1; b < scene.services.size(); ++b)
        {
            Service const& sa = scene.services[a];
            Service const& sb = scene.services[b];
            if ((sa.kind | sb.kind) & CoaCityLife::PLACE_OBJECT_KINDS)
                continue;  // the ways people take: between the NPCs' services, not to every mailbox
            float const from[3] = {sa.y, sa.z, sa.x};
            float const to[3] = {sb.y, sb.z, sb.x};
            float fromPt[3], toPt[3];
            dtPolyRef const fromRef = Snap(*q.query, q.filter, from, fromPt);
            dtPolyRef const toRef = Snap(*q.query, q.filter, to, toPt);
            if (!fromRef || !toRef)
                continue;
            scene.anyTile = true;
            dtPolyRef polys[MAX_POLYS];
            int count = 0;
            if (dtStatusFailed(q.query->findPath(fromRef, toRef, fromPt, toPt, &q.filter, polys, &count, MAX_POLYS)) ||
                !count)
                continue;
            float straight[MAX_POINTS * 3];
            int points = 0;
            q.query->findStraightPath(fromPt, toPt, polys, count, straight, nullptr, nullptr, &points, MAX_POINTS);
            for (int i = 1; i < points; ++i)
                scene.segments.push_back({straight[(i - 1) * 3 + 2], straight[(i - 1) * 3], straight[i * 3 + 2],
                                          straight[i * 3]});
        }
    return scene;
}

// Why (x, y, z) on the navmesh is no square (nullptr: it is one); nearby: the services in range.
char const* PlaceReason(Scene const& scene, Query& q, Map* map, float x, float y, float z, uint32& kinds, uint32& nearby)
{
    kinds = 0;
    nearby = 0;
    for (Service const& s : scene.services)
    {
        float const d = std::sqrt((s.x - x) * (s.x - x) + (s.y - y) * (s.y - y) + (s.z - z) * (s.z - z));
        if (d < ((s.kind & CoaCityLife::PLACE_OBJECT_KINDS) ? CoaCityLife::PLACE_OBJECT_MIN : CoaCityLife::PLACE_NPC_MIN))
            return "too close to a service (its door, its counter)";
        if (d <= CoaCityLife::PLACE_SERVICE_MAX)
        {
            kinds |= s.kind;
            ++nearby;
        }
    }
    // 3.2.2 (sonde of 5f662017: the user's 5 spots still refused were all "on a way between two services", none
    // unfit): a place may lie on a way; bots go through one another, a few standing there block nobody.
    PositionFullTerrainStatus status;
    map->GetFullTerrainStatusForPosition(PHASEMASK_NORMAL, x, y, z + 0.5f, 2.0f, status);
    if (status.liquidInfo.Status != LIQUID_MAP_NO_WATER)
        return "in water";
    // An open space, indoors too (Ironforge, Undercity): walls away, a high ceiling.
    float const head = z + CoaCityLife::PLACE_HEAD;
    uint32 open = 0;
    for (uint32 i = 0; i < CoaCityLife::PLACE_OPEN_DIRECTIONS; ++i)
    {
        float const angle = float(i) * 6.2831853f / float(CoaCityLife::PLACE_OPEN_DIRECTIONS);
        if (map->isInLineOfSight(x, y, head, x + CoaCityLife::PLACE_OPEN_REACH * std::cos(angle),
                                 y + CoaCityLife::PLACE_OPEN_REACH * std::sin(angle), head, PHASEMASK_NORMAL,
                                 LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2))
            ++open;
    }
    bool const ceiling = map->isInLineOfSight(x, y, head, x, y, head + CoaCityLife::PLACE_CEILING, PHASEMASK_NORMAL,
                                              LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2);
    if (!CoaCityLife::PlaceOpen(open, ceiling))
        return ceiling ? "walls close around (fewer than 6 of 8 directions free for 4 yards)"
                       : "a low ceiling (something within 2.5 yards over the head)";
    for (uint32 slot = 0; slot < 3; ++slot)
    {
        float sx = 0.0f, sy = 0.0f;
        CoaCityLife::PlaceSlot(slot, x, y, sx, sy);
        float const around[3] = {sy, z, sx};
        float aroundPt[3];
        if (!Snap(*q.query, q.filter, around, aroundPt) || std::fabs(aroundPt[1] - z) > CoaCityLife::PLACE_FLAT_DZ)
            return "not flat around it (a slot off the floor or 0.6 yard higher or lower)";
    }
    if (OnCityFloor(q, x, y, z) != 1)
        return "not on the city's floor (fewer than 600 polygons joined within 250 yards)";
    return nullptr;
}

// One capital's squares; false when its tiles are not loaded yet (tried again later).
bool FindPlaces(std::pair<uint32, uint32> const& key)
{
    uint32 const mapId = key.first, zoneId = key.second;
    Query q;
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map || !Open(mapId, q))
        return true;  // nothing to find
    Scene const scene = BuildScene(mapId, zoneId, q);
    if (scene.services.size() < 2)
        return true;
    if (!scene.anyTile)
        return false;  // the capital's tiles are not loaded yet
    float minX = scene.services[0].x, maxX = minX, minY = scene.services[0].y, maxY = minY;
    // 3.2.2 (sonde of 1e991b2d: no place in Ironforge, Darnassus): the floors around the services' heights, not z 0.
    float minZ = scene.services[0].z, maxZ = minZ;
    for (Service const& s : scene.services)
    {
        minZ = std::min(minZ, s.z);
        maxZ = std::max(maxZ, s.z);
        minX = std::min(minX, s.x);
        maxX = std::max(maxX, s.x);
        minY = std::min(minY, s.y);
        maxY = std::max(maxY, s.y);
    }
    struct Found
    {
        float x, y, z;
        uint32 nearby;
    };
    std::vector<Found> found;
    for (float gx = minX - 20.0f; gx <= maxX + 20.0f; gx += CoaCityLife::PLACE_GRID)
        for (float gy = minY - 20.0f; gy <= maxY + 20.0f; gy += CoaCityLife::PLACE_GRID)
        {
            // Every floor of the navmesh at (gx, gy).
            float const center[3] = {gy, (minZ + maxZ) * 0.5f, gx};
            float const box[3] = {0.5f, (maxZ - minZ) * 0.5f + 60.0f, 0.5f};
            dtPolyRef polys[16];
            int count = 0;
            q.query->queryPolygons(center, box, &q.filter, polys, &count, 16);
            for (int i = 0; i < count; ++i)
            {
                float on[3];
                bool over = false;
                if (dtStatusFailed(q.query->closestPointOnPoly(polys[i], center, on, &over)) || !over)
                    continue;
                uint32 kinds = 0, nearby = 0;
                if (!PlaceReason(scene, q, map, on[2], on[0], on[1], kinds, nearby))
                    found.push_back({on[2], on[0], on[1], nearby});
            }
        }
    // The most central first, PLACE_SPACING apart.
    std::sort(found.begin(), found.end(), [](Found const& a, Found const& b) { return a.nearby > b.nearby; });
    std::vector<CoaPathProbe::Place> kept;
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    for (Found const& f : found)
    {
        if (kept.size() >= CoaCityLife::PLACES_PER_CAPITAL)
            break;
        bool apart = true;
        for (CoaPathProbe::Place const& p : kept)
            if (std::hypot(p.x - f.x, p.y - f.y) < CoaCityLife::PLACE_SPACING)
                apart = false;
        if (!apart)
            continue;
        CoaPathProbe::Place place;
        place.id = ++placeIds;
        place.mapId = mapId;
        place.x = f.x;
        place.y = f.y;
        place.z = f.z;
        place.capital = zone ? zone->area_name[0] : std::to_string(zoneId);
        kept.push_back(place);
    }
    std::lock_guard<std::mutex> guard(placesLock);
    places[key] = std::move(kept);
    return true;
}

// One capital a call, those not searched yet; their tiles must be loaded.
void FindNextPlaces()
{
    for (auto const& [key, lists] : CapitalSpots())
    {
        {
            std::lock_guard<std::mutex> guard(placesLock);
            if (placesTried.count(key))
                continue;
        }
        bool const done = FindPlaces(key);
        std::lock_guard<std::mutex> guard(placesLock);
        if (done)
            placesTried.insert(key);
        return;
    }
}
}  // namespace

namespace CoaPathProbe
{
std::vector<Place> PlacesNear(uint32_t mapId, float x, float y, float range)
{
    std::vector<Place> nearby;
    std::lock_guard<std::mutex> guard(placesLock);
    for (auto const& [key, list] : places)
        if (key.first == mapId)
            for (Place const& p : list)
                if (std::hypot(p.x - x, p.y - y) <= range)
                    nearby.push_back(p);
    return nearby;
}

std::vector<std::string> PlacesReport()
{
    std::vector<std::string> lines;
    std::lock_guard<std::mutex> guard(placesLock);
    for (auto const& [key, list] : places)
    {
        std::string line = (list.empty() ? std::to_string(key.second) : list[0].capital) + ": " +
                           std::to_string(list.size()) + " squares";
        for (Place const& p : list)
            line += Fmt(" | %.1f %.1f %.1f", p.x, p.y, p.z);
        lines.push_back(line);
    }
    lines.push_back("bots put back on the city's floor from a closed patch: " + std::to_string(rescued.exchange(0)) +
                    " (found on one: " + std::to_string(rescueSeen.exchange(0)) + ")");
    lines.push_back(std::to_string(placesTried.size()) + " of " + std::to_string(CapitalSpots().size()) +
                    " capitals searched; " + std::to_string(placesTaken.exchange(0)) + " bots settled on a square since the last report");
    {
        std::lock_guard<std::mutex> seatGuard(seatsLock);
        uint32 count[3] = {0, 0, 0};
        std::string sample;
        uint32 samples = 0;
        for (auto const& [guid, seen] : seatsHeld)
        {
            ++count[seen.state];
            if (seen.state == 2 && samples < 3)
            {
                ++samples;
                sample += " | " + seen.name + Fmt(" %.2f %.2f %.2f go %.0f ", seen.x, seen.y, seen.z, seen.entry) +
                          (seen.use < 0 ? "refused" : seen.use > 0 ? "stood up" : "unknown");
            }
        }
        lines.push_back("seats: " + std::to_string(seatsUsed.exchange(0)) + " used, " +
                        std::to_string(seatsRefused.exchange(0)) + " refused (SeatUsable), " +
                        std::to_string(seatsSatAgain.exchange(0)) + " sat again; held " +
                        std::to_string(count[0]) + " chair-sit / " + std::to_string(count[1]) + " sit / " +
                        std::to_string(count[2]) + " stand" + sample);
        seatsHeld.clear();
    }
    {
        std::lock_guard<std::mutex> stillGuard(stillLock);
        std::string zones;
        for (auto const& [zone, count] : stillByZone)
            zones += (zones.empty() ? "" : ", ") + zone + " " + std::to_string(count);
        std::string samples;
        for (std::string const& sample : unstickSamples)
            samples += " | " + sample;
        lines.push_back(std::string("city unstick") + (cityUnstickOn ? "" : " (off: counted, not moved)") + ": " +
                        std::to_string(unstuckBuried.exchange(0)) + " in the floor, " +
                        std::to_string(unstuckStill.exchange(0)) + " still, " + std::to_string(unstickCapped.exchange(0)) +
                        " over the hour's cap, " + std::to_string(unstickNoArrival.exchange(0)) +
                        " with no checked arrival; found in capitals: " + (zones.empty() ? "none" : zones) + samples);
        stillByZone.clear();
        unstickSamples.clear();
        unstickHour = 0;
    }
    return lines;
}

void NotePlaceTaken() { ++placesTaken; }

void NoteSeatUse(bool used) { ++(used ? seatsUsed : seatsRefused); }

void NoteSeatSatAgain() { ++seatsSatAgain; }

void NoteSeatHeld(Player* bot, WorldObject const* seat, int8_t use)
{
    if (!bot || !seat)
        return;
    uint8 const stand = bot->getStandState();
    SeatSeen seen;
    seen.state = (stand >= UNIT_STAND_STATE_SIT_LOW_CHAIR && stand <= UNIT_STAND_STATE_SIT_HIGH_CHAIR) ? 0
                 : stand == UNIT_STAND_STATE_SIT                                                     ? 1
                                                                                                     : 2;
    seen.use = use;
    seen.name = bot->GetName();
    seen.x = bot->GetPositionX();
    seen.y = bot->GetPositionY();
    seen.z = bot->GetPositionZ();
    seen.entry = seat->GetEntry();
    std::lock_guard<std::mutex> guard(seatsLock);
    seatsHeld[bot->GetGUID().GetRawValue()] = std::move(seen);
}

bool NpcStopAside() { return npcStopAsideOn; }

bool CityUnstick(Player* bot, bool wandering)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->InBattleground() ||
        bot->IsBeingTeleported() || bot->GetMap()->Instanceable() || !capitalsReady)
        return false;
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && member->GetSession() && !member->GetSession()->IsBot())
                    return false;  // in a group with a player: the player leads it
    uint32 const zoneId = bot->GetZoneId();
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId);
    if (!zone || !(zone->flags & AREA_FLAG_CAPITAL))
        return false;
    uint32 const now = getMSTime();
    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    bool checkFloor = false, still = false;
    {
        std::lock_guard<std::mutex> guard(stillLock);
        StillState& state = stillBots[bot->GetGUID().GetRawValue()];
        if (!state.lastMs || std::fabs(x - state.x) > 1.0f || std::fabs(y - state.y) > 1.0f ||
            std::fabs(z - state.z) > 1.0f)
        {
            state.x = x;
            state.y = y;
            state.z = z;
            state.stillMs = 0;
        }
        else
            state.stillMs = wandering ? state.stillMs + CoaSafeMove::StillGain(getMSTimeDiff(state.lastMs, now)) : 0;
        state.lastMs = now;
        still = state.stillMs >= stillMinutes * MINUTE * IN_MILLISECONDS;
        checkFloor = !state.buriedAt || getMSTimeDiff(state.buriedAt, now) >= CoaSafeMove::BURIED_EVERY_MS;
        if (checkFloor)
            state.buriedAt = now;
        if (!still && !checkFloor)
            return false;
        if (!CoaSafeMove::UnstickAllowed(state.unstickAt, getMSTimeDiff(state.unstickAt, now), unstickHour))
        {
            if (unstickHour >= CoaSafeMove::UNSTICK_PER_HOUR)
                ++unstickCapped;
            return false;
        }
    }
    // In the floor: the navmesh's floor (the city's) over the bot by more than CITY_FLOOR_AGREE yards.
    bool buried = false;
    float meshZ = z;
    if (checkFloor)
    {
        dtNavMesh const* navMesh = bot->GetMap()->GetMapCollisionData().GetMMapData().GetNavMesh();
        Query q;
        q.query = navMesh ? MMAP::MMapMgr::CreateNavMeshQuery(const_cast<dtNavMesh*>(navMesh)) : nullptr;
        q.filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA | NAV_SLIME);
        q.filter.setExcludeFlags(0);
        float const at[3] = {y, z, x};
        float on[3];
        if (q.query && Snap(*q.query, q.filter, at, on))
        {
            meshZ = on[1];
            buried = CoaSafeMove::BuriedUnder(z, meshZ) && OnCityFloor(q, on[2], on[0], on[1]) == 1;
        }
    }
    if (!buried && !still)
        return false;
    {
        std::lock_guard<std::mutex> guard(stillLock);
        ++stillByZone[zone->area_name[0]];
        if (unstickSamples.size() < 3)
            unstickSamples.push_back(bot->GetName() + (buried ? " in the floor" : " still") +
                                     Fmt(" %.1f %.1f %.1f (navmesh %.1f)", x, y, z, meshZ));
    }
    // The capital's nearest arrival, as the arrival check put it (CapitalSpots, ArrivalVerdict).
    auto const capital = CapitalSpots().find({bot->GetMapId(), zoneId});
    float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f, bestDist = -1.0f;
    if (capital != CapitalSpots().end())
        for (Spot const& spot : capital->second.first)
        {
            float ax = spot.x, ay = spot.y, az = spot.z;
            Arrival const kind = ArrivalVerdict(bot->GetMapId(), ax, ay, az);
            if (kind != Arrival::Ok && kind != Arrival::Moved)
                continue;
            float const dist = std::hypot(ax - x, ay - y);
            if (dist > 1.0f && (bestDist < 0.0f || dist < bestDist))
            {
                bestX = ax;
                bestY = ay;
                bestZ = az;
                bestDist = dist;
            }
        }
    if (bestDist < 0.0f)
    {
        ++unstickNoArrival;
        return false;
    }
    ++(buried ? unstuckBuried : unstuckStill);
    LOG_INFO("playerbots.coa", "city unstick: {} {} at map {} {:.1f} {:.1f} {:.1f} (navmesh {:.1f}) -> arrival {:.1f} {:.1f} {:.1f}{}",
             bot->GetName(), buried ? "in the floor" : "still", bot->GetMapId(), x, y, z, meshZ, bestX, bestY, bestZ,
             cityUnstickOn ? "" : " (off: not moved)");
    {
        std::lock_guard<std::mutex> guard(stillLock);
        StillState& state = stillBots[bot->GetGUID().GetRawValue()];
        state.unstickAt = now ? now : 1;
        state.stillMs = 0;
    }
    if (!cityUnstickOn)
        return false;
    ++unstickHour;
    bot->NearTeleportTo(bestX, bestY, bestZ, bot->GetOrientation());
    return true;
}

namespace
{
std::mutex rescueLock;
std::unordered_map<uint64, uint32> rescueChecked;  // bot -> when last checked (getMSTime)
}  // namespace

bool RescueFromPatch(Player* bot)
{
    constexpr uint32 EVERY_MS = 60 * 1000;
    if (!bot || !bot->IsInWorld() || bot->isMoving() || bot->IsInCombat() || bot->GetMap()->Instanceable())
        return false;
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
    if (!zone || !(zone->flags & AREA_FLAG_CAPITAL))
        return false;
    {
        std::lock_guard<std::mutex> guard(rescueLock);
        uint32& last = rescueChecked[bot->GetGUID().GetRawValue()];
        if (last && GetMSTimeDiffToNow(last) < EVERY_MS)
            return false;
        last = getMSTime();
        if (rescueChecked.size() > 20000)
            rescueChecked.clear();
    }
    // A query of our own, on the bot's map thread (the map's tiles are its own).
    dtNavMesh const* navMesh = bot->GetMap()->GetMapCollisionData().GetMMapData().GetNavMesh();
    if (!navMesh)
        return false;
    Query q;
    q.query = MMAP::MMapMgr::CreateNavMeshQuery(const_cast<dtNavMesh*>(navMesh));
    q.filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA | NAV_SLIME);
    q.filter.setExcludeFlags(0);
    if (!q.query)
        return false;
    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    if (OnCityFloor(q, x, y, z) != 0)
        return false;  // on the city's floor, or no polygon here
    for (float ring : {2.0f, 4.0f, 6.0f, 8.0f, 10.0f})
        for (uint32 i = 0; i < 8; ++i)
        {
            float const angle = float(i) * 6.2831853f / 8.0f;
            for (float dz : {0.0f, 2.0f, -2.0f, 4.0f})
            {
                float const probe[3] = {y + ring * std::sin(angle), z + dz, x + ring * std::cos(angle)};
                float on[3];
                if (!Snap(*q.query, q.filter, probe, on) || OnCityFloor(q, on[2], on[0], on[1]) != 1)
                    continue;
                ++rescueSeen;
                // Seen by a real player: left for its next teleport (CoaArrivalCheck), never moved in view.
                Map::PlayerList const& players = bot->GetMap()->GetPlayers();
                for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                    if (Player* player = itr->GetSource())
                        if (player != bot && player->GetSession() && !player->GetSession()->IsBot() &&
                            player->IsWithinDist(bot, 100.0f) && player->IsWithinLOSInMap(bot))
                            return false;
                ++rescued;
                bot->NearTeleportTo(on[2], on[0], on[1] + 0.05f, bot->GetOrientation());
                return true;
            }
        }
    return false;
}

std::vector<std::string> GroundProbe(uint32_t mapId, float x, float y, float z)
{
    std::vector<std::string> lines;
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return {"no map " + std::to_string(mapId)};
    auto read = [&](char const* when)
    {
        PositionFullTerrainStatus status;
        map->GetFullTerrainStatusForPosition(PHASEMASK_NORMAL, x, y, z, 2.0f, status);
        lines.push_back(std::string(when) + (map->IsGridLoaded(x, y) ? " (grid loaded)" : " (grid NOT loaded)") +
                        Fmt(": terrain %.2f, GetHeight terrain only %.2f, with the vmap %.2f", map->GetGridHeight(x, y),
                            map->GetHeight(PHASEMASK_NORMAL, x, y, z + 0.5f, false),
                            map->GetHeight(PHASEMASK_NORMAL, x, y, z + 0.5f, true)) +
                        Fmt(", floor %.2f, ", status.floorZ) + (status.outdoors ? "outdoors" : "indoors"));
    };
    read("as it is");
    if (!map->IsGridLoaded(x, y))
    {
        map->LoadGrid(x, y);
        read("grid loaded now");
    }
    return lines;
}

std::vector<std::string> PlaceExplain(uint32_t mapId, float x, float y, float z)
{
    std::vector<std::string> lines;
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (map && !map->IsGridLoaded(x, y))
        map->LoadGrid(x, y);
    Query q;
    if (!map || !Open(mapId, q))
    {
        lines.push_back("no navmesh for map " + std::to_string(mapId));
        return lines;
    }
    float const at[3] = {y, z, x};
    float atPt[3];
    dtPolyRef const atRef = Snap(*q.query, q.filter, at, atPt);
    if (!atRef)
    {
        lines.push_back("no polygon there");
        return lines;
    }
    uint32 const zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, x, y, z);
    Scene const scene = BuildScene(mapId, zoneId, q);
    dtPolyRef around[CITY_SCAN];
    int aroundCount = 0;
    q.query->findPolysAroundCircle(atRef, atPt, CITY_RADIUS, &q.filter, around, nullptr, nullptr, &aroundCount, CITY_SCAN);
    lines.push_back(Fmt("snaps to %.2f %.2f %.2f, ", atPt[2], atPt[0], atPt[1]) +
                    Fmt("%.0f polygons joined within 250 yards, ", aroundCount) +
                    Fmt("%.0f services in the zone, %.0f ways", scene.services.size(), scene.segments.size()));
    float nearest = 1e9f;
    uint32 nearestKind = 0;
    for (Service const& s : scene.services)
    {
        float const d = std::sqrt((s.x - atPt[2]) * (s.x - atPt[2]) + (s.y - atPt[0]) * (s.y - atPt[0]) +
                                  (s.z - atPt[1]) * (s.z - atPt[1]));
        if (d < nearest)
        {
            nearest = d;
            nearestKind = s.kind;
        }
    }
    uint32 kinds = 0, nearby = 0;
    char const* why = PlaceReason(scene, q, map, atPt[2], atPt[0], atPt[1], kinds, nearby);
    lines.push_back(Fmt("services within 50 yards: %.0f (kinds %.0f: 1 auction, 2 bank, 4 inn, 8 flight, 16 quest ", nearby,
                        kinds) + Fmt("board, 32 mailbox); nearest %.1f yards (kind %.0f)", nearest, nearestKind));
    lines.push_back(why ? std::string("not a square: ") + why : std::string("a square"));
    float closest = 1e9f;
    std::string which = "none found yet";
    {
        std::lock_guard<std::mutex> guard(placesLock);
        for (auto const& [key, list] : places)
            if (key.first == mapId)
                for (Place const& p : list)
                    if (float const d = std::hypot(p.x - atPt[2], p.y - atPt[0]); d < closest)
                    {
                        closest = d;
                        which = Fmt("%.1f %.1f %.1f, ", p.x, p.y, p.z) + Fmt("%.1f yards off", d);
                    }
    }
    lines.push_back("nearest square found: " + which);
    return lines;
}
}  // namespace CoaPathProbe

namespace
{
class CoaPathProbeWorldScript : public WorldScript
{
    uint32 placesTimer = 0;
    uint32 reportTimer = 0;

public:
    CoaPathProbeWorldScript()
        : WorldScript("CoaPathProbeWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        arrivalCheck = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaArrivalCheck", true, false);
        placesOn = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.City.Squares", true, false);
        cityUnstickOn = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.CityUnstick.Enable", true, false);
        npcStopAsideOn = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.NpcStopAside", true, false);
        stillMinutes =
            std::max<uint32>(5, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.CityUnstick.StillMinutes", 25, false));
        bool const on = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaPathCheck", false, false);
        checkDelayMs = on ? 1000 * std::max<uint32>(
                                       1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaPathCheck.DelaySeconds", 300, false))
                          : 0;
    }

    void OnUpdate(uint32 diff) override
    {
        // 3.2.2: the capitals' squares, one capital every few seconds until all are searched, and the hourly line.
        placesTimer += diff;
        if (placesOn && placesTimer >= 5000)
        {
            placesTimer = 0;
            FindNextPlaces();
        }
        reportTimer += diff;
        if (placesOn && reportTimer >= HOUR * IN_MILLISECONDS)
        {
            reportTimer = 0;
            for (std::string const& line : CoaPathProbe::PlacesReport())
                LOG_INFO("playerbots.coa", "city squares (last hour): {}", line);
        }
        // 3.2.2: one teleport arrival judged a tick, on the world thread (out of the maps' updates).
        if (arrivalCheck)
        {
            std::tuple<uint32, int32, int32> key;
            std::tuple<float, float, float> point;
            bool due = false;
            {
                std::lock_guard<std::mutex> guard(arrivalsLock);
                if (!pendingArrivals.empty())
                {
                    key = pendingArrivals.begin()->first;
                    point = pendingArrivals.begin()->second;
                    pendingArrivals.erase(pendingArrivals.begin());
                    due = true;
                }
            }
            if (due)
                JudgeArrival(key, std::get<0>(point), std::get<1>(point), std::get<2>(point));
        }
        if (!checkDelayMs || checkDone)
            return;
        checkTimer += diff;
        if (checkTimer < checkDelayMs)
            return;
        checkDone = true;
        for (std::string const& line : CoaPathProbe::CheckCapitals())
            LOG_INFO("playerbots.coa", "path check: {}", line);
    }
};
}  // namespace

void AddSC_coa_path_probe() { new CoaPathProbeWorldScript(); }
