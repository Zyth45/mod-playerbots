/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WCActions.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "AiObjectContext.h"
#include "Group.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "Timer.h"

using namespace DungeonBoss;
using namespace WailingCaverns;

namespace
{
    // Where each bot is running to, so the next one picks another spot instead of the same few yards.
    struct Reservation
    {
        float x = 0.0f;
        float y = 0.0f;
        uint32 until = 0;
    };

    std::mutex reservationLock;
    std::unordered_map<ObjectGuid, Reservation> reservations;

    constexpr float SIDE_STEPS[] = { 3.0f, 6.0f, 9.0f, 13.0f };
    constexpr float BACK_STEPS[] = { 4.0f, 8.0f };
    constexpr float CROWD_RADIUS = 4.0f;
    constexpr float CROWD_PENALTY = 8.0f;
    constexpr float MAX_CLIMB = 5.0f;
    constexpr float RESERVATION_MS = 6000.0f;

    struct Spot
    {
        float x;
        float y;
    };

    std::vector<Spot> Candidates(OverrunLine const& line, float halfWidth, float botX, float botY)
    {
        float const cosA = std::cos(line.angle);
        float const sinA = std::sin(line.angle);
        float const dx = botX - line.x;
        float const dy = botY - line.y;
        float const along = std::clamp(dx * cosA + dy * sinA, 0.0f, OVERRUN_LENGTH);
        float const aside = dy * cosA - dx * sinA;

        auto const at = [&](float a, float s) { return Spot{ line.x + a * cosA - s * sinA, line.y + a * sinA + s * cosA }; };

        std::vector<Spot> spots;
        for (float step : SIDE_STEPS)
        {
            spots.push_back(at(along, halfWidth + step));
            spots.push_back(at(along, -(halfWidth + step)));
        }
        // Behind Skum the charge never comes; beside his tail is often the shortest way out of a pen.
        for (float step : BACK_STEPS)
        {
            spots.push_back(at(-(halfWidth + step), aside));
            spots.push_back(at(-(halfWidth + step), 0.0f));
        }
        return spots;
    }

    float PathLength(Movement::PointsArray const& points)
    {
        float length = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
            length += std::sqrt((points[i] - points[i - 1]).squaredLength());
        return length;
    }
}

bool WcMoveFromOverrunAction::Execute(Event /*event*/)
{
    Unit* skum = AI_VALUE2(Unit*, "find target", "skum");
    std::optional<OverrunLine> const line = ActiveOverrun(skum);
    float const halfWidth = skum ? OverrunHalfWidth(skum, bot) : 0.0f;
    if (!line || !InOverrunPath(*line, halfWidth, bot->GetPositionX(), bot->GetPositionY()))
        return false;

    uint32 const now = getMSTime();
    std::vector<Reservation> taken;
    {
        std::lock_guard<std::mutex> guard(reservationLock);
        auto own = reservations.find(bot->GetGUID());
        if (own != reservations.end() && own->second.until > now &&
            !InOverrunPath(*line, halfWidth, own->second.x, own->second.y))
        {
            float z = bot->GetPositionZ();
            bot->UpdateAllowedPositionZ(own->second.x, own->second.y, z);
            return MoveTo(bot->GetMapId(), own->second.x, own->second.y, z, false, false, false, false,
                          MovementPriority::MOVEMENT_COMBAT);
        }
        for (auto const& [guid, reservation] : reservations)
            if (guid != bot->GetGUID() && reservation.until > now)
                taken.push_back(reservation);
    }

    std::vector<Spot> others;
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && member->IsAlive() && member->GetMap() == bot->GetMap())
                    others.push_back({ member->GetPositionX(), member->GetPositionY() });
    for (Reservation const& reservation : taken)
        others.push_back({ reservation.x, reservation.y });

    float bestScore = std::numeric_limits<float>::max();
    float bestX = 0.0f, bestY = 0.0f, bestZ = 0.0f;
    for (Spot const& spot : Candidates(*line, halfWidth, bot->GetPositionX(), bot->GetPositionY()))
    {
        float z = bot->GetPositionZ();
        bot->UpdateAllowedPositionZ(spot.x, spot.y, z);
        if (std::fabs(z - bot->GetPositionZ()) > MAX_CLIMB)
            continue;

        PathGenerator path(bot);
        path.CalculatePath(spot.x, spot.y, z, false);
        if (path.GetPathType() != PATHFIND_NORMAL || path.GetPath().empty())
            continue;
        G3D::Vector3 const end = path.GetPath().back();
        if (InOverrunPath(*line, halfWidth, end.x, end.y))
            continue;

        uint32 crowd = 0;
        for (Spot const& other : others)
            if (std::hypot(other.x - end.x, other.y - end.y) < CROWD_RADIUS)
                ++crowd;
        float const score = PathLength(path.GetPath()) + crowd * CROWD_PENALTY;
        if (score < bestScore)
        {
            bestScore = score;
            bestX = end.x;
            bestY = end.y;
            bestZ = end.z;
        }
    }

    if (bestScore == std::numeric_limits<float>::max())
        return MoveOutOfPath(skum, line->angle, halfWidth, SIDE_STEPS[1]);

    {
        std::lock_guard<std::mutex> guard(reservationLock);
        reservations[bot->GetGUID()] = { bestX, bestY, now + uint32(RESERVATION_MS) };
    }
    return MoveTo(bot->GetMapId(), bestX, bestY, bestZ, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
}
