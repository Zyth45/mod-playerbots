/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_STANDOFF_MATH_H
#define COA_STANDOFF_MATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Where the combat regroup walks a bot, from Dungeon Clear (DungeonClearMath::StandoffCandidates,
// PointShortOfPathEnd, IsRegroupAnchorCandidate), pure and unchanged.
namespace CoaStandoffMath
{
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // The fight anchor is looked for this far at most (Dungeon Clear's DC_ENGAGEMENT_RADIUS).
    inline constexpr float PI = 3.14159265358979323846f;
    inline constexpr float ENGAGEMENT_RADIUS = 100.0f;
    // A route "ends on the level" of its target within this (DC_Z_LEVEL_TOLERANCE).
    inline constexpr float Z_LEVEL_TOLERANCE = 5.0f;
    // Below this the regroup moves at combat priority, beyond it at normal priority (DC_COMBAT_APPROACH_RANGE).
    inline constexpr float COMBAT_APPROACH_RANGE = 10.0f;

    // `ringPoints + 1` points on a circle of `radius` around the center, the first on the bot's side, the others
    // fanned out alternately to either side. Z is the center's; the caller grounds them. Bot on the center: +X.
    inline std::vector<Point> StandoffCandidates(Point const& center, Point const& bot, float radius,
                                                 std::uint32_t ringPoints)
    {
        std::vector<Point> out;
        out.reserve(ringPoints + 1);
        float const dx = bot.x - center.x;
        float const dy = bot.y - center.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        float const baseAngle = len < 0.01f ? 0.0f : std::atan2(dy, dx);
        auto emit = [&](float angle)
        {
            out.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius, center.z});
        };
        emit(baseAngle);
        float const step = (2.0f * PI) / float(ringPoints + 1);
        for (std::uint32_t k = 1; k <= ringPoints; ++k)
        {
            float const off = step * float((k + 1) / 2);
            float const sign = (k % 2 == 1) ? 1.0f : -1.0f;
            emit(baseAngle + sign * off);
        }
        return out;
    }

    // The point `stopShort` yards before the END of a route, measured ALONG it (a straight line through a wall is
    // not a walk). A route no longer than `stopShort` returns its first point; an empty one the origin.
    inline Point PointShortOfPathEnd(std::vector<Point> const& path, float stopShort)
    {
        if (path.empty())
            return Point{};
        float remaining = std::max(0.0f, stopShort);
        for (std::size_t i = path.size() - 1; i > 0; --i)
        {
            Point const& a = path[i - 1];
            Point const& b = path[i];
            float const seg = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
            if (seg >= remaining)
            {
                if (seg <= 0.0f)
                    return b;
                float const f = remaining / seg;
                return {b.x + (a.x - b.x) * f, b.y + (a.y - b.y) * f, b.z + (a.z - b.z) * f};
            }
            remaining -= seg;
        }
        return path.front();
    }

    // A DPS anchors on a unit holding the tank in combat only when it can fight it: inside the engagement radius and
    // reachable on its level. Nothing qualifying: the tank is the anchor.
    inline bool IsRegroupAnchorCandidate(float dist, float engagementRadius, bool levelReachable)
    {
        return dist <= engagementRadius && levelReachable;
    }
}

#endif
