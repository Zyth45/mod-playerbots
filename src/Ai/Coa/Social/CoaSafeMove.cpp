/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSafeMove.h"

#include "CoaSafeMoveRules.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

#include <algorithm>
#include <cmath>
#include <list>
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
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

bool StandByNpc(Player* bot, WorldObject const* npc, float& x, float& y, float& z)
{
    if (!bot || !npc || !bot->IsInWorld() || bot->GetMapId() != npc->GetMapId())
        return false;
    if (CanWalkTo(bot, x, y, z))
        return true;
    PathGenerator path(bot);
    if (!path.CalculatePath(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), false))
        return false;
    uint32 const type = uint32(path.GetPathType());
    if (CoreStraightLine(type, uint32(path.GetPath().size())))
        return false;
    G3D::Vector3 end = path.GetActualEndPosition();
    float endToNpc = npc->GetExactDist(end.x, end.y, end.z);
    // 3.2: no bridge over a dead end short of the NPC (BridgesDeadEnd): on the living bench the bots at the end of it
    // (Stormwind's counter) stood there until their wander ended, the stop by the NPC failing the flat line.
    // 3.2.1: the end may be short only because the core took a closed island right under the NPC (Chilton): the
    // same point a little higher (NpcLift) gets the floor the bot walks on.
    for (uint32 step = 1; step <= NPC_LIFT_STEPS && !StandsByNpc(endToNpc); ++step)
    {
        PathGenerator lifted(bot);
        if (!lifted.CalculatePath(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ() + NpcLift(step), false) ||
            CoreStraightLine(uint32(lifted.GetPathType()), uint32(lifted.GetPath().size())))
            continue;
        G3D::Vector3 const& liftedEnd = lifted.GetActualEndPosition();
        float const liftedToNpc = npc->GetExactDist(liftedEnd.x, liftedEnd.y, liftedEnd.z);
        if (liftedToNpc < endToNpc)
        {
            end = liftedEnd;
            endToNpc = liftedToNpc;
        }
    }
    if (!StandsByNpc(endToNpc) || !npc->IsWithinLOS(end.x, end.y, end.z + 1.5f))
        return false;
    x = end.x;
    y = end.y;
    z = end.z;
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
                       npc->IsWithinLOS(end.x, end.y, end.z + 1.5f));
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
