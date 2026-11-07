/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DungeonBossHelpers.h"

#include "AiObjectContext.h"
#include "Group.h"
#include "Map.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace DungeonBoss
{
    namespace
    {
        bool NameIs(SpellInfo const* info, char const* name)
        {
            return info && info->SpellName[LOCALE_enUS] && std::strcmp(info->SpellName[LOCALE_enUS], name) == 0;
        }

        constexpr float RANGE_MARGIN = 2.0f;
    }

    bool HasAuraNamed(Unit const* unit, char const* name)
    {
        for (auto const& [spellId, application] : unit->GetAppliedAuras())
            if (NameIs(application->GetBase()->GetSpellInfo(), name))
                return true;
        return false;
    }

    int32 AuraRemainingNamed(Unit const* unit, char const* name)
    {
        for (auto const& [spellId, application] : unit->GetAppliedAuras())
            if (NameIs(application->GetBase()->GetSpellInfo(), name))
                return application->GetBase()->GetDuration();
        return -1;
    }

    bool IsCastingNamed(Unit const* unit, char const* name)
    {
        for (uint32 type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
            if (Spell* spell = unit->GetCurrentSpell(type))
                if (NameIs(spell->GetSpellInfo(), name))
                    return true;
        return false;
    }

    float RelativeAngle(Unit const* center, Unit const* unit)
    {
        float const angle = Position::NormalizeOrientation(center->GetAngle(unit) - center->GetOrientation());
        return angle > float(M_PI) ? angle - 2.0f * float(M_PI) : angle;
    }

    bool InFront(Unit const* unit, Player* bot, float reach, float halfArc)
    {
        return Within(bot, unit, reach) && std::fabs(RelativeAngle(unit, bot)) < halfArc + CONE_MARGIN;
    }

    Unit* ConeOver(PlayerbotAI* botAI, AiObjectContext* context, Player* bot,
                   std::span<FrontalCone const> cones, FrontalCone const*& cone)
    {
        for (FrontalCone const& candidate : cones)
        {
            Unit* boss = AI_VALUE2(Unit*, "find target", candidate.boss);
            if (!boss || !boss->IsAlive() || boss->GetVictim() == bot)
                continue;

            if (candidate.spell && !IsCastingNamed(boss, candidate.spell) && !HasAuraNamed(boss, candidate.spell))
                continue;

            if (candidate.spell && CastTarget(boss, candidate.spell) == bot)
                continue;

            if (InFront(boss, bot, candidate.reach, candidate.halfArc))
            {
                cone = &candidate;
                return boss;
            }
        }
        return nullptr;
    }

    bool Within(Player* bot, Unit const* unit, float radius)
    {
        return bot->GetExactDist2d(unit) < KeepAwayDistance(unit, radius);
    }

    float KeepAwayDistance(Unit const* unit, float radius)
    {
        return radius + unit->GetCombatReach() + RANGE_MARGIN;
    }

    Unit* CastTarget(Unit const* caster, char const* spell)
    {
        for (uint32 type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
            if (Spell* cast = caster->GetCurrentSpell(type))
                if (NameIs(cast->GetSpellInfo(), spell))
                    return cast->m_targets.GetUnitTarget();
        return nullptr;
    }

    bool InPath(Unit const* source, float angle, float length, float halfWidth, Unit const* unit)
    {
        float const dx = unit->GetPositionX() - source->GetPositionX();
        float const dy = unit->GetPositionY() - source->GetPositionY();
        float const along = dx * std::cos(angle) + dy * std::sin(angle);
        float const aside = dy * std::cos(angle) - dx * std::sin(angle);
        return along > -halfWidth && along < length + halfWidth && std::fabs(aside) < halfWidth;
    }

    std::vector<Position> RingAround(float x, float y, float z, std::initializer_list<float> radii, uint32 directions)
    {
        std::vector<Position> spots;
        for (float radius : radii)
            for (uint32 i = 0; i < directions; ++i)
            {
                float const angle = 2.0f * float(M_PI) * i / directions;
                spots.emplace_back(x + std::cos(angle) * radius, y + std::sin(angle) * radius, z);
            }
        return spots;
    }

    bool MovesIntoDanger(Action* action)
    {
        if (dynamic_cast<DungeonBossMovementAction*>(action) || dynamic_cast<AvoidAoeAction*>(action) ||
            action->getName() == "coa boss dodge")
            return false;

        return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action);
    }
}

using namespace DungeonBoss;

bool DungeonBossMovementAction::MoveAround(Unit* boss, float angle, float distance)
{
    float const absolute = Position::NormalizeOrientation(boss->GetOrientation() + angle);
    return MoveToSpot(boss->GetPositionX() + std::cos(absolute) * distance,
                      boss->GetPositionY() + std::sin(absolute) * distance);
}

bool DungeonBossMovementAction::MoveOutOfPath(Unit* source, float angle, float halfWidth, float step)
{
    float const dx = bot->GetPositionX() - source->GetPositionX();
    float const dy = bot->GetPositionY() - source->GetPositionY();
    float const along = dx * std::cos(angle) + dy * std::sin(angle);
    float const aside = dy * std::cos(angle) - dx * std::sin(angle);
    float const out = aside >= 0.0f ? halfWidth + step : -(halfWidth + step);
    return MoveToSpot(source->GetPositionX() + along * std::cos(angle) - out * std::sin(angle),
                      source->GetPositionY() + along * std::sin(angle) + out * std::cos(angle));
}

bool DungeonBossMovementAction::MoveToSpot(float x, float y)
{
    float z = bot->GetPositionZ();
    if (!bot->GetMap()->CheckCollisionAndGetValidCoords(bot, bot->GetPositionX(), bot->GetPositionY(),
                                                        bot->GetPositionZ(), x, y, z))
        return false;

    return MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
}

bool DungeonBossMovementAction::MoveOutOfCone(Unit* boss, FrontalCone const& cone)
{
    float const side = cone.halfArc + CONE_SIDE_STEP;
    float const angle = RelativeAngle(boss, bot) >= 0.0f ? side : -side;
    float const distance = std::max(bot->GetExactDist2d(boss), boss->GetCombatReach() + 2.0f);
    return MoveAround(boss, angle, distance);
}

namespace
{
    struct ReservedSpot
    {
        float x = 0.0f;
        float y = 0.0f;
        uint32 until = 0;
    };

    std::mutex reservedLock;
    std::unordered_map<ObjectGuid, ReservedSpot> reservedSpots;

    constexpr float SPOT_CROWD_RADIUS = 4.0f;
    constexpr float SPOT_CROWD_PENALTY = 8.0f;
    constexpr float SPOT_MAX_CLIMB = 5.0f;
    constexpr uint32 SPOT_RESERVATION_MS = 5000;

    float PathLength(Movement::PointsArray const& points)
    {
        float length = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
            length += std::sqrt((points[i] - points[i - 1]).squaredLength());
        return length;
    }
}

bool DungeonBossMovementAction::MoveToReachableSpot(std::vector<Position> const& candidates,
                                                    std::function<bool(float, float, float)> const& safe, float maxPath)
{
    uint32 const now = getMSTime();
    std::vector<std::pair<float, float>> others;
    {
        std::lock_guard<std::mutex> guard(reservedLock);
        for (auto const& [guid, spot] : reservedSpots)
            if (guid != bot->GetGUID() && spot.until > now)
                others.emplace_back(spot.x, spot.y);
    }
    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != bot && member->IsAlive() && member->GetMap() == bot->GetMap())
                    others.emplace_back(member->GetPositionX(), member->GetPositionY());

    float bestScore = std::numeric_limits<float>::max();
    G3D::Vector3 best;
    for (Position const& candidate : candidates)
    {
        float z = bot->GetPositionZ();
        bot->UpdateAllowedPositionZ(candidate.GetPositionX(), candidate.GetPositionY(), z);
        if (std::fabs(z - bot->GetPositionZ()) > SPOT_MAX_CLIMB)
            continue;

        PathGenerator path(bot);
        path.CalculatePath(candidate.GetPositionX(), candidate.GetPositionY(), z, false);
        if (path.GetPathType() != PATHFIND_NORMAL || path.GetPath().empty())
            continue;
        G3D::Vector3 const end = path.GetPath().back();
        float const length = PathLength(path.GetPath());
        if ((maxPath > 0.0f && length > maxPath) || !safe(end.x, end.y, end.z))
            continue;

        uint32 crowd = 0;
        for (auto const& [x, y] : others)
            if (std::hypot(x - end.x, y - end.y) < SPOT_CROWD_RADIUS)
                ++crowd;
        float const score = length + crowd * SPOT_CROWD_PENALTY;
        if (score < bestScore)
        {
            bestScore = score;
            best = end;
        }
    }

    if (bestScore == std::numeric_limits<float>::max())
        return false;

    {
        std::lock_guard<std::mutex> guard(reservedLock);
        reservedSpots[bot->GetGUID()] = { best.x, best.y, now + SPOT_RESERVATION_MS };
    }
    return MoveTo(bot->GetMapId(), best.x, best.y, best.z, false, false, false, false, MovementPriority::MOVEMENT_COMBAT);
}

bool DungeonBossMovementAction::KeepAway(Unit* unit, float radius)
{
    float const wanted = KeepAwayDistance(unit, radius);
    float const distance = bot->GetExactDist2d(unit);
    if (distance >= wanted)
        return false;

    return MoveAway(unit, wanted - distance);
}
