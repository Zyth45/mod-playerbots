/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_HEAL_MATH_H
#define COA_HEAL_MATH_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// The decisions of the healer guards that came from Dungeon Clear (07/11), apart from the game objects so that
// tests/coa_gardes_soins_math.cpp can check them. The callers gather the live state; nothing here reads the world.
namespace CoaHealMath
{
    // "Nobody needs healing", returned by SelectHealTarget.
    inline constexpr std::size_t HealTargetNone = static_cast<std::size_t>(-1);

    // One candidate of the line-of-sight-blind heal target. Deliberately no line-of-sight or range flag: the stock
    // `party member to heal` drops an out-of-sight member entirely, which is why a healer stopped healing a tank
    // dragged round a corner (Dungeon Clear, DungeonClearMath::HealCandidate). `isReferenceTank` marks the group's
    // reference tank (CoaGroupTank), the one being kited.
    struct HealCandidate
    {
        float healthPct;
        bool isReferenceTank;
    };

    // Among the members BELOW `hpFloor` (on raw health, so a healthy tank never steals the pick from a hurt DPS),
    // the lowest of `healthPct - (isReferenceTank ? tankBias : 0)`. HealTargetNone when nobody is below the floor.
    // Same choice as Dungeon Clear's DungeonClearMath::SelectHealTarget.
    inline std::size_t SelectHealTarget(std::vector<HealCandidate> const& members, float hpFloor, float tankBias)
    {
        std::size_t best = HealTargetNone;
        float bestScore = 0.0f;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            HealCandidate const& m = members[i];
            if (m.healthPct >= hpFloor)
                continue;
            float const score = m.healthPct - (m.isReferenceTank ? tankBias : 0.0f);
            if (best == HealTargetNone || score < bestScore)
            {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    struct Point2
    {
        float x;
        float y;
    };

    // Standoff points on a circle of `radius` around `center`, the first on the side the healer already stands
    // (shortest move, most likely to round the same corner), the others fanning out alternately to either side;
    // `ringPoints` past the first. A healer standing on the center gets +X first. Dungeon Clear's
    // DungeonClearMath::StandoffCandidates, without the game's Position.
    inline std::vector<Point2> StandoffRing(Point2 center, Point2 healer, float radius, std::uint32_t ringPoints)
    {
        constexpr float Pi = 3.14159265358979323846f;
        std::vector<Point2> out;
        out.reserve(ringPoints + 1);
        float const dx = healer.x - center.x;
        float const dy = healer.y - center.y;
        float const base = std::sqrt(dx * dx + dy * dy) < 0.01f ? 0.0f : std::atan2(dy, dx);
        auto emit = [&](float angle)
        { out.push_back({ center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius }); };
        emit(base);
        float const step = (2.0f * Pi) / float(ringPoints + 1);
        for (std::uint32_t k = 1; k <= ringPoints; ++k)
        {
            float const off = step * float((k + 1) / 2);
            emit(base + ((k % 2 == 1) ? off : -off));
        }
        return out;
    }

    // The standoff radius: a little INSIDE heal range so a step of the target does not drop the healer straight back
    // out, never under 5 yd so it does not stack on the target (Dungeon Clear, DungeonClearHealRepositionAction).
    inline float StandoffRadius(float healRange)
    {
        return std::max(5.0f, healRange * 0.6f);
    }

    // A path the healer can take really ends where it was asked: a player's path to a point off the mesh is clamped
    // to the nearest polygon, which across a wall or a floor lies on the healer's side (Dungeon Clear,
    // DcEngageGeometry::PathedCloseOn, 5 yd; the vertical search of its navmesh snap, 10 yd, for a ring point whose
    // height is only the target's).
    inline bool PathEndsAt(float endX, float endY, float endZ, float x, float y, float z, float flatTolerance,
                           float heightTolerance)
    {
        return std::fabs(endZ - z) <= heightTolerance && std::hypot(endX - x, endY - y) <= flatTolerance;
    }

    // The point `stopShort` yards before the END of a route, measured ALONG it: the fallback when no ring point
    // validates, "walk at the target and let the corner give you sight". A point on the straight healer-target line
    // lies in the wall across a hairpin (Karazhan's Maiden: healers walked into it and dropped to the floor below,
    // Dungeon Clear, DungeonClearMath::PointShortOfPathEnd); a point on the route is on the mesh. A route no longer
    // than `stopShort` gives its first point, an empty one (0, 0, 0). `P` has x, y, z (G3D::Vector3).
    template <class P>
    void PointShortOfPathEnd(std::vector<P> const& path, float stopShort, float& x, float& y, float& z)
    {
        x = y = z = 0.0f;
        if (path.empty())
            return;
        float remaining = std::max(0.0f, stopShort);
        for (std::size_t i = path.size() - 1; i > 0; --i)
        {
            P const& a = path[i - 1];
            P const& b = path[i];
            float const seg = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
            if (seg >= remaining)
            {
                float const f = seg <= 0.0f ? 0.0f : remaining / seg;
                x = b.x + (a.x - b.x) * f;
                y = b.y + (a.y - b.y) * f;
                z = b.z + (a.z - b.z) * f;
                return;
            }
            remaining -= seg;
        }
        x = path.front().x;
        y = path.front().y;
        z = path.front().z;
    }

    // Whether the healer walks to heal (Dungeon Clear, DungeonClearHealRepositionTrigger::IsActive), in its order.
    struct RepositionInputs
    {
        bool enabled;            // AiPlayerbot.CoaHealReposition
        bool isHealer;
        bool isReferenceTank;    // the healer tanks for the group: it never walks off to heal
        bool crowdControlled;    // stunned, fleeing, confused, rooted
        bool inCombat;           // the healer
        bool targetInCombat;     // the heal target
        bool hasTarget;          // someone below the floor, alive, resolved
        bool visibleHurtInSight; // the stock `party member to heal` is under medium health and in sight
        float targetDistance;    // 2D
        float maxRange;          // AiPlayerbot.CoaHealRepositionMaxRange
        bool targetInSight;
        float healRange;
    };

    inline bool ShouldReposition(RepositionInputs const& in)
    {
        if (!in.enabled || !in.isHealer || in.isReferenceTank || in.crowdControlled || !in.hasTarget)
            return false;
        // Out of a fight nothing is dying: outside a Dungeon Clear run the healer follows its player, and walking to a
        // member below 90% after every fight would fight that follow. Dungeon Clear ran it in both engines because its
        // run drives the healer; here it acts out of combat only while the target is in one (a healer nobody hits is
        // not in combat while the group fights).
        if (!in.inCombat && !in.targetInCombat)
            return false;
        // Visible heal work first: the stock heal stack does it this tick.
        if (in.visibleHurtInSight)
            return false;
        // That far is a wipe or a skip, not a reposition.
        if (in.targetDistance > in.maxRange)
            return false;
        return !in.targetInSight || in.targetDistance > in.healRange;
    }

    // The out-of-combat top-off floor (Dungeon Clear, DcPartyState::RestMinHpPct): the configured health target,
    // else 90% clamped to the health bots eat back up to (AiPlayerbot.AlmostFullHealth), so the healer never tops
    // off above what the group restores anyway.
    inline float TopOffFloor(std::uint32_t configured, std::uint32_t almostFullHealth)
    {
        if (configured)
            return float(std::min<std::uint32_t>(configured, 100));
        return std::min(90.0f, float(almostFullHealth));
    }

    // Clamps a setting to the range Dungeon Clear allowed for it (DcSettingsRegistry).
    inline float Clamp(float value, float low, float high)
    {
        return std::min(high, std::max(low, value));
    }
}

#endif
