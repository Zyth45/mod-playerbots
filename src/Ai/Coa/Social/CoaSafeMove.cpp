/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSafeMove.h"

#include "CoaSafeMoveRules.h"
#include "CoaMoveAudit.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <unordered_map>
#include <mutex>
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Timer.h"
#include "Player.h"

static_assert(CoaSafeMove::NORMAL == PATHFIND_NORMAL && CoaSafeMove::SHORTCUT == PATHFIND_SHORTCUT &&
                  CoaSafeMove::INCOMPLETE == PATHFIND_INCOMPLETE && CoaSafeMove::NOPATH == PATHFIND_NOPATH &&
                  CoaSafeMove::NOT_USING_PATH == PATHFIND_NOT_USING_PATH && CoaSafeMove::SHORT == PATHFIND_SHORT &&
                  CoaSafeMove::FARFROMPOLY_START == PATHFIND_FARFROMPOLY_START &&
                  CoaSafeMove::FARFROMPOLY_END == PATHFIND_FARFROMPOLY_END,
              "CoaSafeMove flags must match the core's PathType");

namespace CoaSafeMove
{
bool MovePoint(Player* bot, float x, float y, float z, bool allowSwim)
{
    if (!bot || !bot->IsInWorld())
        return false;
    PathGenerator path(bot);
    if (!path.CalculatePath(x, y, z, false))
        return false;
    bool const waterLine = allowSwim && bot->IsInWater() && bot->IsWithinLOS(x, y, z);
    if (!Walkable(uint32(path.GetPathType()), waterLine))
    {
        // Bench: the moves refused (the core would have gone through a wall), with log level debug.
        LOG_DEBUG("playerbots", "coa safe move refused: bot {} map {} from {:.1f} {:.1f} {:.1f} to {:.1f} {:.1f} {:.1f} type {}",
                  bot->GetName(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), x, y,
                  z, uint32(path.GetPathType()));
        return false;
    }
    bot->GetMotionMaster()->MovePoint(0, x, y, z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
    return true;
}
// 3.2: from (fx, fy, fz) to (tx, ty, tz) in a straight line, in sight, the ground staying within FLAT_LINE_DZ of
// the line every FLAT_LINE_STEP yards (FlatLineArrives).
bool FlatLine(Player* bot, float fx, float fy, float fz, float tx, float ty, float tz)
{
    Map* map = bot->GetMap();
    float const length = std::sqrt((tx - fx) * (tx - fx) + (ty - fy) * (ty - fy));
    if (!map || length > FLAT_LINE_MAX ||
        !map->isInLineOfSight(fx, fy, fz + 1.5f, tx, ty, tz + 1.5f, bot->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
                              VMAP::ModelIgnoreFlags::Nothing))
        return false;
    float gap = 0.0f;
    uint32 const steps = std::max<uint32>(1, uint32(length / FLAT_LINE_STEP));
    for (uint32 i = 1; i <= steps && gap <= FLAT_LINE_DZ; ++i)
    {
        float const t = float(i) / float(steps);
        float const px = fx + (tx - fx) * t, py = fy + (ty - fy) * t, pz = fz + (tz - fz) * t;
        float const ground = map->GetHeight(bot->GetPhaseMask(), px, py, pz + 2.0f);
        gap = ground <= INVALID_HEIGHT ? FLAT_LINE_DZ + 1.0f : std::max(gap, std::fabs(ground - pz));
    }
    return FlatLineArrives(length, gap, true);
}

namespace
{
std::mutex ringStopsLock;
// NPC guid -> a stop by it a bot walked to (StandByNpc's success: the direct way, the path's end, or the ring); map
// threads.
std::unordered_map<uint64, G3D::Vector3> ringStops;
// (NPC guid, start cell) -> when a whole ring search from there found nothing: not again from that cell for
// RING_RETRY_MS (up to 16 paths a search).
std::map<std::pair<uint64, uint32>, uint32> ringMisses;
constexpr uint32 RING_RETRY_MS = 60 * 1000;

// 3.2.2: in front of the NPC, as it faces at home (a creature turns to whoever talks to it); an object has no front.
bool FrontOf(WorldObject const* npc, float x, float y)
{
    Creature const* creature = npc->ToCreature();
    if (!creature)
        return true;
    Position const& home = creature->GetHomePosition();
    return InFrontOf(home.GetPositionX(), home.GetPositionY(), home.GetOrientation(), x, y);
}

// 3.2.2: the server sees a floor at the stop (OnServerFloor): no stop on a piece of navmesh between two floors.
bool ServerFloor(WorldObject const* npc, float x, float y, float z)
{
    Map* map = npc->GetMap();
    return !map || OnServerFloor(z, map->GetHeight(npc->GetPhaseMask(), x, y, z + 0.5f));
}

// 3.2.2 (the user's eye at Stormwind's bank: Kirsten Burnside's counter, "derrière les grilles de la banque"): no stop
// behind a banker or an auctioneer, even as a last resort; another NPC with a stop in front is taken instead.
bool Clerk(WorldObject const* npc)
{
    Creature const* creature = npc->ToCreature();
    return creature && creature->HasNpcFlag(NPCFlags(UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_AUCTIONEER));
}

// NPCs with no stop in front of them (their facing in the database turned to a wall?), traced once each.
std::mutex behindLock;
std::unordered_map<uint64, bool> behindTraced;

void TraceBehind(WorldObject const* npc)
{
    {
        std::lock_guard<std::mutex> guard(behindLock);
        if (behindTraced.size() > 4096 || !behindTraced.emplace(npc->GetGUID().GetRawValue(), true).second)
            return;
    }
    LOG_INFO("playerbots.coa", "stand by npc: no stop in front of {} (entry {}, {:.1f} {:.1f} {:.1f}), one behind it kept",
             npc->GetName(), npc->GetEntry(), npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());
}

// 3.2.2: the NPC is seen from the stop, at 1.5 yards up or at the eyes, or the stop is at its counter (SeenOrAtCounter).
bool Sees(WorldObject const* npc, float x, float y, float z)
{
    return SeenOrAtCounter(npc->IsWithinLOS(x, y, z + 1.5f), npc->IsWithinLOS(x, y, z + STAND_EYES),
                           npc->GetExactDist(x, y, z), z - npc->GetPositionZ());
}

// By the NPC: within NPC_STAND_REACH, or within the core's own reach for it (a high counter).
bool ByNpc(Player* bot, WorldObject const* npc, float x, float y, float z)
{
    float const d = npc->GetExactDist(x, y, z);
    return StandsByNpc(d) || InInteractionReach(d, npc->GetObjectSize(), bot->GetObjectSize());
}

void KnowStop(Player* bot, WorldObject const* npc, float x, float y, float z)
{
    if (!ByNpc(bot, npc, x, y, z))
        return;
    uint64 const key = npc->GetGUID().GetRawValue();
    std::lock_guard<std::mutex> guard(ringStopsLock);
    if (ringStops.size() < 4096 || ringStops.count(key))
        ringStops[key] = G3D::Vector3(x, y, z);
}

// A stop by the NPC the bot walks to: the path to (px, py, pz) ends by the NPC and in its sight.
bool RingStopWalks(Player* bot, WorldObject const* npc, float px, float py, float pz, G3D::Vector3& stop)
{
    PathGenerator path(bot);
    if (!path.CalculatePath(px, py, pz, false) ||
        CoreStraightLine(uint32(path.GetPathType()), uint32(path.GetPath().size())))
        return false;
    stop = path.GetActualEndPosition();
    return ByNpc(bot, npc, stop.x, stop.y, stop.z) && FrontOf(npc, stop.x, stop.y) &&
           Sees(npc, stop.x, stop.y, stop.z) && ServerFloor(npc, stop.x, stop.y, stop.z);
}

bool RingStopByNpc(Player* bot, WorldObject const* npc, float& x, float& y, float& z)
{
    uint64 const key = npc->GetGUID().GetRawValue();
    std::pair<uint64, uint32> const missKey(key, StartCell(bot->GetPositionX(), bot->GetPositionY()));
    G3D::Vector3 stop;
    {
        std::lock_guard<std::mutex> guard(ringStopsLock);
        auto found = ringStops.find(key);
        if (found != ringStops.end())
            stop = found->second;
    }
    bool found = stop != G3D::Vector3::zero() && RingStopWalks(bot, npc, stop.x, stop.y, stop.z, stop);
    if (!found)
    {
        std::lock_guard<std::mutex> guard(ringStopsLock);
        auto miss = ringMisses.find(missKey);
        if (miss != ringMisses.end() && GetMSTimeDiffToNow(miss->second) < RING_RETRY_MS)
            return false;
    }
    float const towardBot = npc->GetAngle(bot);
    for (uint32 i = 0; !found && i < STAND_RING_TRIES; ++i)
    {
        float radius = 0.0f, angle = 0.0f;
        StandRingPoint(i, towardBot, radius, angle);
        float const px = npc->GetPositionX() + radius * std::cos(angle);
        float const py = npc->GetPositionY() + radius * std::sin(angle);
        for (uint32 d = 0; !found && d < STAND_RING_DROP_COUNT; ++d)
            found = RingStopWalks(bot, npc, px, py, npc->GetPositionZ() - STAND_RING_DROPS[d], stop);
    }
    {
        std::lock_guard<std::mutex> guard(ringStopsLock);
        if (!found)
        {
            if (ringMisses.size() < 16384 || ringMisses.count(missKey))
                ringMisses[missKey] = getMSTime();
            return false;
        }
        if (ringStops.size() < 4096 || ringStops.count(key))
            ringStops[key] = stop;
        ringMisses.erase(missKey);
    }
    x = stop.x;
    y = stop.y;
    z = stop.z;
    return true;
}
}  // namespace

bool StandByNpc(Player* bot, WorldObject const* npc, float& x, float& y, float& z)
{
    if (!bot || !npc || !bot->IsInWorld() || bot->GetMapId() != npc->GetMapId())
        return false;
    // 3.2.3: an NPC with a void in front of it (SideStopNpc: Tal 2995): a stop by its side, on the server's floor.
    if (Creature const* creature = npc->ToCreature())
        if (SideStopNpc(creature->GetEntry()))
            if (Map* map = npc->GetMap())
                for (int side = 0; side < 2; ++side)
                {
                    float sx, sy;
                    SideStopPoint(npc->GetPositionX(), npc->GetPositionY(), creature->GetHomePosition().GetOrientation(),
                                  side, sx, sy);
                    float const sz = map->GetHeight(npc->GetPhaseMask(), sx, sy, npc->GetPositionZ() + 0.5f);
                    if (SideStopFloor(npc->GetPositionZ(), sz) && CanWalkTo(bot, sx, sy, sz))
                    {
                        x = sx;
                        y = sy;
                        z = sz;
                        KnowStop(bot, npc, x, y, z);
                        return true;
                    }
                }
    // 3.2.2: a stop behind the NPC (in a bank's booth) is kept as a last resort only (FrontOf).
    bool behind = false;
    G3D::Vector3 behindStop;
    if (CanWalkTo(bot, x, y, z) && ServerFloor(npc, x, y, z))
    {
        if (FrontOf(npc, x, y))
        {
            KnowStop(bot, npc, x, y, z);  // 3.2.2: a stop a bot walks to, the reference of UnstickFromIsland
            return true;
        }
        behind = true;
        behindStop = G3D::Vector3(x, y, z);
    }
    PathGenerator path(bot);
    if (path.CalculatePath(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), false) &&
        !CoreStraightLine(uint32(path.GetPathType()), uint32(path.GetPath().size())))
    {
        G3D::Vector3 const& end = path.GetActualEndPosition();
        // 3.2: no bridge over a dead end short of the NPC (BridgesDeadEnd): on the living bench the bots at the end of
        // it (Stormwind's counter) stood there until their wander ended, the stop by the NPC failing the flat line.
        if (ByNpc(bot, npc, end.x, end.y, end.z) && Sees(npc, end.x, end.y, end.z) && ServerFloor(npc, end.x, end.y, end.z))
        {
            if (FrontOf(npc, end.x, end.y))
            {
                x = end.x;
                y = end.y;
                z = end.z;
                KnowStop(bot, npc, x, y, z);
                return true;
            }
            if (!behind)
            {
                behind = true;
                behindStop = end;
            }
        }
    }
    // 3.2.2: the end may be short only because the core took another floor right under the NPC (Chilton), or lie
    // behind it: a stop on a ring around it (StandRingPoint), in front of it, the one found before first.
    if (RingStopByNpc(bot, npc, x, y, z))
        return true;
    if (!behind || Clerk(npc))
        return false;
    TraceBehind(npc);
    x = behindStop.x;
    y = behindStop.y;
    z = behindStop.z;
    return true;
}

char const* NoWayWhy(Player* bot, WorldObject const* npc, float& endToNpc)
{
    endToNpc = 0.0f;
    if (!bot || !npc || !bot->IsInWorld() || bot->GetMapId() != npc->GetMapId())
        return "gone";
    PathGenerator path(bot);
    if (!path.CalculatePath(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), false))
        return "no path";
    G3D::Vector3 const& end = path.GetActualEndPosition();
    endToNpc = npc->GetExactDist(end.x, end.y, end.z);
    return NoWayReason(uint32(path.GetPathType()), uint32(path.GetPath().size()), StandsByNpc(endToNpc),
                       Sees(npc, end.x, end.y, end.z));
}

namespace
{
// A real player (not a bot) on the bot's map who sees it.
bool RealPlayerSees(Player* bot)
{
    constexpr float SIGHT = 100.0f;
    Map* map = bot->GetMap();
    if (!map)
        return true;
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player || player == bot || !player->GetSession() || player->GetSession()->IsBot())
            continue;
        if (player->IsWithinDist(bot, SIGHT) && player->IsWithinLOSInMap(bot))
            return true;
    }
    return false;
}

// The path from (fx, fy, fz) reaches the stop.
bool PathReaches(Player* bot, float fx, float fy, float fz, G3D::Vector3 const& stop)
{
    PathGenerator path(bot);
    if (!path.CalculatePath(fx, fy, fz, stop.x, stop.y, stop.z, false) ||
        CoreStraightLine(uint32(path.GetPathType()), uint32(path.GetPath().size())))
        return false;
    G3D::Vector3 const& end = path.GetActualEndPosition();
    return (end - stop).length() <= 1.5f;
}
}  // namespace

Unstick UnstickFromIsland(Player* bot, WorldObject const* npc)
{
    if (!bot || !npc || !bot->IsInWorld() || bot->GetMapId() != npc->GetMapId())
        return Unstick::NotStuck;
    G3D::Vector3 stop;
    {
        std::lock_guard<std::mutex> guard(ringStopsLock);
        auto found = ringStops.find(npc->GetGUID().GetRawValue());
        if (found == ringStops.end())
            return Unstick::NoKnownStop;  // no stop known to be walkable: nothing to tell an island from
        stop = found->second;
    }
    bool const stuck = !PathReaches(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), stop);
    bool walkable = false;
    float wx = 0.0f, wy = 0.0f, wz = 0.0f;
    Map* map = bot->GetMap();
    for (float radius : UNSTICK_RADII)
        for (uint32 i = 0; stuck && map && !walkable && i < UNSTICK_DIRECTIONS; ++i)
        {
            float const angle = float(i) * 6.2831853f / float(UNSTICK_DIRECTIONS);
            wx = bot->GetPositionX() + radius * std::cos(angle);
            wy = bot->GetPositionY() + radius * std::sin(angle);
            wz = map->GetHeight(bot->GetPhaseMask(), wx, wy, bot->GetPositionZ() + 2.0f);
            walkable = wz > INVALID_HEIGHT &&
                       FlatLine(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), wx, wy, wz) &&
                       PathReaches(bot, wx, wy, wz, stop);
        }
    Unstick const choice = UnstickChoice(stuck, walkable, !walkable && stuck && RealPlayerSees(bot));
    if (choice == Unstick::Walk)
    {
        CoaMoveAudit::NoteOrigin(bot, "walk off a navmesh island");
        bot->GetMotionMaster()->MovePoint(0, wx, wy, wz, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, false, false);
    }
    else if (choice == Unstick::Teleport)
    {
        CoaMoveAudit::NoteOrigin(bot, "teleport off a navmesh island");
        bot->NearTeleportTo(stop.x, stop.y, stop.z, bot->GetOrientation());
    }
    return choice;
}

bool InNpcBooth(Player* bot, float x, float y, float z)
{
    if (!bot || !bot->IsInWorld())
        return false;
    std::list<Creature*> clerks;
    struct Check
    {
        float x, y, z;
        bool operator()(Creature* c) const
        {
            return c->IsAlive() && c->HasNpcFlag(NPCFlags(UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_AUCTIONEER)) &&
                   c->GetExactDist2d(x, y) <= BOOTH_RANGE;
        }
    } check{x, y, z};
    Acore::CreatureListSearcher<Check> searcher(bot, clerks, check);
    Cell::VisitObjects(x, y, bot->GetMap(), searcher, BOOTH_RANGE);
    for (Creature* clerk : clerks)
        if (InBooth(clerk->GetExactDist2d(x, y), z - clerk->GetPositionZ(), FrontOf(clerk, x, y)))
            return true;
    return false;
}

ObjectGuid OtherReachableAuctioneer(Player* bot, WorldObject* npc)
{
    constexpr float AUCTIONEERS_RANGE = 40.0f;
    if (!bot || !npc || !bot->IsInWorld() || bot->GetMapId() != npc->GetMapId())
        return ObjectGuid::Empty;
    std::list<Creature*> auctioneers;
    struct Check
    {
        WorldObject const* from;
        bool operator()(Creature* c) const
        {
            return c->IsAlive() && c->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER) && from->IsWithinDist(c, AUCTIONEERS_RANGE);
        }
    } check{npc};
    Acore::CreatureListSearcher<Check> searcher(npc, auctioneers, check);
    Cell::VisitObjects(npc, searcher, AUCTIONEERS_RANGE);
    auctioneers.sort([bot](Creature* a, Creature* b) { return bot->GetExactDist(a) < bot->GetExactDist(b); });
    for (Creature* other : auctioneers)
    {
        if (other->GetGUID() == npc->GetGUID())
            continue;
        float x = other->GetPositionX(), y = other->GetPositionY(), z = other->GetPositionZ();
        if (StandByNpc(bot, other, x, y, z))
            return other->GetGUID();
    }
    return ObjectGuid::Empty;
}

bool AsideStopOk(Player* bot, WorldObject const* npc, float& x, float& y, float& z)
{
    if (!bot || !npc || !bot->IsInWorld() || bot->GetMapId() != npc->GetMapId())
        return false;
    Map* map = npc->GetMap();
    if (!map)
        return false;
    float const nx = npc->GetPositionX(), ny = npc->GetPositionY(), nz = npc->GetPositionZ();
    // No wall or counter between the NPC and the stop: the navmesh raycast and the collision checks reach it.
    float cx = x, cy = y, cz = nz;
    if (!map->CheckCollisionAndGetValidCoords(npc, nx, ny, nz, cx, cy, cz) ||
        (cx - x) * (cx - x) + (cy - y) * (cy - y) > 0.25f)
        return false;
    float const floorZ = map->GetHeight(npc->GetPhaseMask(), x, y, nz + 0.5f);
    if (!SideStopFloor(nz, floorZ))
        return false;
    if (Avoided(bot->GetMapId(), x, y, floorZ) || InNpcBooth(bot, x, y, floorZ))
        return false;
    if (!npc->IsWithinLOS(x, y, floorZ + 1.5f))
        return false;
    if (!CanWalkTo(bot, x, y, floorZ))
        return false;
    z = floorZ;
    return true;
}

bool CanWalkTo(Player* bot, float x, float y, float z, bool* straight)
{
    if (straight)
        *straight = false;
    if (!bot || !bot->IsInWorld())
        return false;
    // As the core will follow it (CoreStraightLine): a cut path is fine, only the straight line is refused. Refusing
    // every cut path (3.1.6 lot 4) trapped bots inside the bank: from in there, a long path is cut at the point limit.
    PathGenerator path(bot);
    if (!path.CalculatePath(x, y, z, false))
        return false;
    uint32 const type = uint32(path.GetPathType());
    uint32 const points = uint32(path.GetPath().size());
    // 3.2: a straight line in sight over flat ground arrives (Undercity's auctioneers), and so does the last leg
    // past a dead end (Stormwind's counter, after StandByNpc's bridge).
    auto flat = [&]()
    {
        bool const ok = FlatLine(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), x, y, z);
        if (ok && straight)
            *straight = true;
        return ok;
    };
    if (CoreStraightLine(type, points))
        return flat();
    // A short cut path to a target out of reach ends on a dead end (a ledge by the Stormwind bank, 3.2).
    G3D::Vector3 const& end = path.GetActualEndPosition();
    float const gap = std::sqrt((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y) + (end.z - z) * (end.z - z));
    return !DeadEndPath(type, points, gap) || flat();
}
}  // namespace CoaSafeMove
