/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaHazardVacate.h"

#include "CoaGardesConfig.h"
#include "CoaGroupTank.h"
#include "CoaHazard.h"
#include "DetourExtended.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "Map.h"
#include "MapCollisionData.h"
#include "MapDefines.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "Timer.h"

#include <cmath>

namespace
{
    // Read once: the guard is asked every tick by every bot, and a configuration lookup walks the option keys.
    bool VacateEnabled()
    {
        static bool const enabled = CoaConfigBool("AiPlayerbot.HazardVacate", "DungeonClear.HazardVacate", true);
        return enabled;
    }

    // Dungeon Clear's NavmeshSnap::Snap, unchanged: the nearest walkable polygon to (x,y,z) within `maxRadius`
    // horizontally and 10yd vertically, with one retry at twice the radius.
    struct Snapped
    {
        bool ok{false};
        float x{0.0f}, y{0.0f}, z{0.0f};
    };

    Snapped SnapToNavmesh(Map const* map, float x, float y, float z, float maxRadius)
    {
        Snapped result;
        if (!map)
            return result;

        MapCollisionData& coll = const_cast<Map*>(map)->GetMapCollisionData();
        dtNavMesh const* navMesh = coll.GetMMapData().GetNavMesh();
        dtNavMeshQuery const* query = coll.GetMMapData().GetNavMeshQuery();
        if (!navMesh || !query)
            return result;

        dtQueryFilterExt filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA);
        filter.setExcludeFlags(0);

        float const point[3] = {y, z, x};
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            float const radius = attempt == 0 ? maxRadius : maxRadius * 2.0f;
            float const extents[3] = {radius, 10.0f, radius};
            dtPolyRef polyRef = 0;
            float closest[3] = {0.0f, 0.0f, 0.0f};
            if (dtStatusSucceed(query->findNearestPoly(point, extents, &filter, &polyRef, closest)) && polyRef != 0)
            {
                result.ok = true;
                result.x = closest[2];
                result.y = closest[0];
                result.z = closest[1];
                return result;
            }
        }

        return result;
    }

    // Dungeon Clear's DcEngageGeometry::IsNavReachableWithin, unchanged: a normal path whose length stays within
    // the detour bound of the straight line ("a path exists" cannot tell 6yd from 6yd through a wall, 60yd round).
    bool IsNavReachableWithin(Player* bot, Position const& p, float ratio, float slack)
    {
        if (!bot)
            return false;

        PathGenerator gen(bot);
        gen.CalculatePath(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ());
        if (gen.GetPathType() != PATHFIND_NORMAL)
            return false;

        float const straight = bot->GetExactDist(&p);
        return gen.getPathLength() <= CoaHazard::DetourBound(straight, ratio, slack);
    }
}

bool CoaHazardVacateTrigger::IsActive()
{
    // Instances only (2.9.4), before any setting or hazard read.
    if (!CoaGroupTank::GuardScope(bot) || bot->isDead())
        return false;

    // Cheap map early-out before anything touches game state. HasAnyHazard, not HasEmitters: Scholomance registers
    // only a ground pool and a creature-only gate would make the retreat inert there.
    if (!CoaHazardRegistry::HasAnyHazard(bot->GetMapId()))
        return false;

    if (!VacateEnabled() || !CoaGroupTank::ModuleGuardActs(bot, CoaGroundHazardsGuard))
        return false;

    // Cannot move out of the pulse while rooted/stunned/etc: do not claim the tick from something that might.
    if (bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED | UNIT_STATE_ROOT))
        return false;

    return CoaHazard::NearestVacate(bot).ok;
}

bool CoaHazardVacateAction::Execute(Event /*event*/)
{
    if (!CoaGroupTank::GuardScope(bot))
        return false;
    // One sample of the live hazard world answers every question below: nothing here can move an emitter.
    CoaHazard::LiveSet const live = CoaHazard::Sample(bot);

    // Re-read here (trigger/action gap); bail if it despawned or the bot cleared in between.
    CoaHazard::VacateEmitter const danger =
        CoaHazard::NearestVacate(live, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    if (!danger.ok)
    {
        _fleeToValid = false;
        return false;
    }

    Map* map = bot->GetMap();
    if (!map)
        return false;

    // COMMIT to one destination and ride it out while it stays clean and the bot is still walking to it: with a field
    // of emitters, re-planning every tick re-elects a different centre and reverses the bearing (net travel zero in
    // the middle of eight sludges). Time-capped, so a committed point is never unsupervised.
    uint32 const nowMs = getMSTime();
    if (_fleeToValid && bot->isMoving() && GetMSTimeDiffToNow(_fleeSetAtMs) < CoaHazard::VacateCommitMaxMs &&
        bot->GetExactDist2d(&_fleeTo) > 3.0f &&
        !CoaHazard::PointIsInVacateBand(live, _fleeTo.GetPositionX(), _fleeTo.GetPositionY(),
                                        _fleeTo.GetPositionZ()))
        return true;

    // Aim past the pulse rim by THIS row's slack, measured from the emitter centre.
    float const clearDist = danger.pulseRadius + danger.retreatSlack;

    // Bearing directly away from the emitter; the bot's own facing if it stands exactly on the centre.
    float const ex = danger.x, ey = danger.y;
    float away = std::atan2(bot->GetPositionY() - ey, bot->GetPositionX() - ex);
    if (!std::isfinite(away) || (bot->GetPositionX() == ex && bot->GetPositionY() == ey))
        away = bot->GetOrientation();

    // Straight away first, then fan the bearing out symmetrically so a wall behind the bot does not trap it.
    static constexpr float kFan[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f, 2.4f, -2.4f, 3.14159f};

    // Best SECOND choice: clears this pulse and opens distance, but still sits in another band. Seeded with where the
    // bot stands, so it is only taken when it really moves the bot away.
    float bestPartialClear = bot->GetExactDist2d(ex, ey);
    float bestPartialX = 0.0f, bestPartialY = 0.0f, bestPartialZ = 0.0f;
    bool havePartial = false;

    for (float delta : kFan)
    {
        float const ang = away + delta;
        float const cx = ex + clearDist * std::cos(ang);
        float const cy = ey + clearDist * std::sin(ang);

        Snapped const snap = SnapToNavmesh(map, cx, cy, bot->GetPositionZ(), 12.0f);
        if (!snap.ok)
            continue;

        // The snap can pull the point back toward the emitter: keep it only if it truly clears the pulse.
        float const dxp = snap.x - ex, dyp = snap.y - ey;
        float const fromEmitter = std::sqrt(dxp * dxp + dyp * dyp);
        if (fromEmitter < danger.pulseRadius)
            continue;

        if (fromEmitter > bestPartialClear + 2.0f)
        {
            bestPartialClear = fromEmitter;
            bestPartialX = snap.x;
            bestPartialY = snap.y;
            bestPartialZ = snap.z;
            havePartial = true;
        }

        // Not into another hazard's placement keep-out...
        if (CoaHazard::PointIsHot(live, snap.x, snap.y, snap.z))
            continue;

        // ...nor into any live danger band, or the trigger fires again on arrival, forever.
        if (CoaHazard::PointIsInVacateBand(live, snap.x, snap.y, snap.z))
            continue;

        // Reachable AND actually near.
        Position const cand(snap.x, snap.y, snap.z);
        if (!IsNavReachableWithin(bot, cand, CoaHazard::VacateDetourRatio, CoaHazard::VacateDetourSlack))
            continue;

        MoveTo(map->GetId(), snap.x, snap.y, snap.z, /*idle*/ false, /*react*/ false, /*normal_only*/ false,
               /*exact_waypoint*/ false, MovementPriority::MOVEMENT_COMBAT);
        _fleeTo = Position(snap.x, snap.y, snap.z, 0.0f);
        _fleeToValid = true;
        _fleeSetAtMs = nowMs;
        return true;
    }

    // Nothing fully clears: take the best partial if it is a genuinely short walk (one path query, not ten).
    _fleeToValid = false;
    if (havePartial)
    {
        Position const partial(bestPartialX, bestPartialY, bestPartialZ);
        if (IsNavReachableWithin(bot, partial, CoaHazard::VacateDetourRatio, CoaHazard::VacateDetourSlack))
        {
            MoveTo(map->GetId(), bestPartialX, bestPartialY, bestPartialZ, /*idle*/ false, /*react*/ false,
                   /*normal_only*/ false, /*exact_waypoint*/ false, MovementPriority::MOVEMENT_COMBAT);
            return true;
        }
    }

    // Boxed in on every bearing: yield the tick. Standing in a pulse one more tick is a bounded cost; being walked
    // out of the room by an unvalidated point is not.
    return false;
}

void CoaGroundHazardsStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("coa hazard vacate", {NextAction("coa hazard vacate", 55.0f)}));
}
