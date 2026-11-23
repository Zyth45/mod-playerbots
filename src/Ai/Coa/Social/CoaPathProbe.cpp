/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaPathProbe.h"

#include "Chat.h"
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace
{
constexpr int MAX_POLYS = 1024;
constexpr int MAX_POINTS = 256;
uint32 checkDelayMs = 0;  // AiPlayerbot.CoaPathCheck: 0 off, else the delay after the start
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
    lines.push_back(Fmt("start snaps to %.2f %.2f %.2f (%.2f yd off)", fromPt[2], fromPt[0], fromPt[1],
                        std::sqrt(dtVdistSqr(from, fromPt))));
    lines.push_back(Fmt("target snaps to %.2f %.2f %.2f (%.2f yd off)", toPt[2], toPt[0], toPt[1],
                        std::sqrt(dtVdistSqr(to, toPt))));
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
    // Every floor stacked under the target (a narrow, tall box), each reached or not.
    float const box[3] = {0.6f, 6.0f, 0.6f};
    dtPolyRef floors[32];
    int floorCount = 0;
    query.queryPolygons(to, box, &q.filter, floors, &floorCount, 32);
    std::map<int, std::pair<float, bool>> heights;  // tenths of a yard -> height, reached
    for (int i = 0; i < floorCount; ++i)
    {
        float closest[3];
        bool over = false;
        if (dtStatusFailed(query.closestPointOnPoly(floors[i], to, closest, &over)) || !over)
            continue;
        int reached = 0;
        dtPolyRef way[MAX_POLYS];
        bool const in = Joined(query, q.filter, fromRef, fromPt, floors[i], closest, way, reached);
        auto& floor = heights[int(std::lround(closest[1] * 10.0f))];
        floor.first = closest[1];
        floor.second = floor.second || in;
    }
    std::string floorsText = "floors under the target:";
    for (auto const& [tenth, floor] : heights)
        floorsText += Fmt(" z %.2f", floor.first) + (floor.second ? " reached;" : " NOT reached;");
    lines.push_back(heights.empty() ? std::string("no floor right under the target") : floorsText);
    return lines;
}

std::vector<std::string> CheckCapitals()
{
    struct Spot
    {
        uint32 entry;
        float x, y, z;
    };
    // (map, capital zone) -> arrivals and targets, from the spawns.
    std::map<std::pair<uint32, uint32>, std::pair<std::vector<Spot>, std::vector<Spot>>> capitals;
    uint32 const arrivalFlags = UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_FLIGHTMASTER;
    uint32 const targetFlags = UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_FLIGHTMASTER;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
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
            lists.second.push_back({data.id, data.posX, data.posY, data.posZ});
    }
    std::vector<std::string> lines;
    uint32 pairs = 0, cut = 0;
    for (auto const& [key, lists] : capitals)
    {
        Query q;
        if (!Open(key.first, q))
            continue;
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(key.second);
        std::string const name = zone ? zone->area_name[0] : std::to_string(key.second);
        for (Spot const& from : lists.first)
        {
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
                dtPolyRef polys[MAX_POLYS];
                int count = 0;
                if (fromRef && toRef && Joined(*q.query, q.filter, fromRef, fromPt, toRef, toPt, polys, count))
                    continue;
                ++cut;
                lines.push_back(name + Fmt(": from the arrival of %.0f (%.1f %.1f", from.entry, from.x, from.y) +
                                Fmt(" %.1f) to %.0f: ", from.z, to.entry) +
                                (!fromRef ? "no polygon at the arrival" : !toRef ? "no polygon at the target"
                                                                                  : "not joined"));
            }
        }
    }
    lines.push_back("capitals' path check: " + std::to_string(pairs) + " pairs, " + std::to_string(cut) +
                    " not joined (tiles not loaded count as not joined)");
    return lines;
}
}  // namespace CoaPathProbe

namespace
{
class CoaPathProbeWorldScript : public WorldScript
{
public:
    CoaPathProbeWorldScript()
        : WorldScript("CoaPathProbeWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        bool const on = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaPathCheck", false, false);
        checkDelayMs = on ? 1000 * std::max<uint32>(
                                       1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaPathCheck.DelaySeconds", 300, false))
                          : 0;
    }

    void OnUpdate(uint32 diff) override
    {
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
