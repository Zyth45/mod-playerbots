/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaGardesSoins.h"

#include "CoaGardesConfig.h"
#include "CoaGroupTank.h"
#include "CoaHealEscort.h"
#include "CoaHealMath.h"

#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

#include <string>

namespace
{
    // Guard names shared with Dungeon Clear (CoaGroupTank::SetHandledByDungeonClear).
    constexpr char const* GuardHealReposition = "heal reposition";
    constexpr char const* GuardTopOffParty = "top off party";

    // Eye height of the line-of-sight ray from a standoff point (Dungeon Clear, FindStandoffPoint).
    constexpr float EyeHeight = 2.0f;
    // Standoff points tried past the first (Dungeon Clear: 7, eight points round the target).
    constexpr uint32 RingPoints = 7;
    // How far the end of a path to a ring point may land from it: the navmesh snap radius Dungeon Clear used (8 yd)
    // and the vertical search of that snap (10 yd), the ring point's height being only the target's.
    constexpr float RingSnapFlat = 8.0f;
    constexpr float RingSnapHeight = 10.0f;
    // A route that "reaches" the target ends within this of it, flat and in height (Dungeon Clear,
    // DC_Z_LEVEL_TOLERANCE).
    constexpr float LevelTolerance = 5.0f;
    // The fallback stops this short of the target, along the route.
    constexpr float CloseOnStopShort = 5.0f;
    // Combat priority only on the final close leg: a long walk back at combat priority plows through every pack on
    // the way instead of stopping to fight what it pulls (Dungeon Clear, DC_COMBAT_APPROACH_RANGE).
    constexpr float CombatApproachRange = 10.0f;

    float ConfigFloat(std::string const& key, std::string const& alias, float fallback, float low, float high)
    {
        std::string const value = CoaConfigString(key, alias, std::to_string(fallback));
        float read = fallback;
        try
        {
            read = std::stof(value);
        }
        catch (...)
        {
            read = fallback;
        }
        return CoaHealMath::Clamp(read, low, high);
    }

    // A unit of the bot's own map, alive and in the world: nothing of another map is ever read.
    bool SameMap(Player* bot, Unit const* unit)
    {
        return unit && unit->IsInWorld() && unit->IsAlive() && unit->FindMap() && unit->FindMap() == bot->FindMap();
    }

    Unit* ResolveSameMap(Player* bot, ObjectGuid guid)
    {
        if (guid.IsEmpty())
            return nullptr;
        Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
        return SameMap(bot, unit) ? unit : nullptr;
    }
}

CoaHealSettings const& CoaHealSettings::Get()
{
    // Read once: the configuration lookup walks every key, far too dear for a value read every 200 ms by every
    // healer. Same defaults and ranges as Dungeon Clear (DcSettingsRegistry).
    static CoaHealSettings const settings = []
    {
        CoaHealSettings s;
        s.healReposition = CoaConfigBool("AiPlayerbot.CoaHealReposition", "DungeonClear.HealReposition", true);
        s.hpFloor = ConfigFloat("AiPlayerbot.CoaHealRepositionHpFloor", "DungeonClear.HealRepositionHpFloor", 90.0f,
                                1.0f, 100.0f);
        s.tankBias = ConfigFloat("AiPlayerbot.CoaHealRepositionTankBias", "DungeonClear.HealRepositionTankBias", 15.0f,
                                 0.0f, 50.0f);
        s.maxRange = ConfigFloat("AiPlayerbot.CoaHealRepositionMaxRange", "DungeonClear.HealRepositionMaxRange", 60.0f,
                                 20.0f, 120.0f);
        s.topOffParty = CoaConfigBool("AiPlayerbot.CoaTopOffParty", "", true);
        int32 const pct = CoaConfigInt("AiPlayerbot.CoaTopOffHealthPct", "DungeonClear.RestHealthPct", 0);
        s.topOffHealthPct = uint32(std::min(100, std::max(0, pct)));
        return s;
    }();
    return settings;
}

ObjectGuid CoaHealTargetValue::Calculate()
{
    // Instances only (2.9.4): nothing outside a group on a dungeon or raid map.
    if (!CoaGroupTank::GuardScope(bot) || !bot->IsInWorld())
        return ObjectGuid::Empty;
    Group* group = bot->GetGroup();
    if (!group)
        return ObjectGuid::Empty;

    CoaHealSettings const& settings = CoaHealSettings::Get();
    // The tank being kited is the one the bias favours: the run leader, the player who tanks, the main tank flag or
    // the first living tank, on this map (CoaGroupTank).
    Player* tank = CoaGroupTank::Reference(bot);
    // The stock heal-candidate radius (healDistance * 2), WITHOUT its line-of-sight filter: keeping the out-of-sight
    // member in candidacy is the whole point.
    float const maxDist = sPlayerbotAIConfig.healDistance * 2.0f;

    std::vector<CoaHealMath::HealCandidate> candidates;
    std::vector<ObjectGuid> guids;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !SameMap(bot, member) || member->IsGameMaster() || member->IsCharmed())
            continue;
        if (bot->GetDistance2d(member) > maxDist)
            continue;
        candidates.push_back({ member->GetHealthPct(), tank && member == tank });
        guids.push_back(member->GetGUID());
    }

    // The escorted creature, so the healer also walks to keep IT in range and sight; never tank-biased.
    if (Unit* escortee = CoaHealEscort::Escortee(bot))
        if (!escortee->IsCharmed() && bot->GetDistance2d(escortee) <= maxDist)
        {
            candidates.push_back({ escortee->GetHealthPct(), false });
            guids.push_back(escortee->GetGUID());
        }

    std::size_t const pick = CoaHealMath::SelectHealTarget(candidates, settings.hpFloor, settings.tankBias);
    return pick == CoaHealMath::HealTargetNone ? ObjectGuid::Empty : guids[pick];
}

Unit* CoaPartyMemberToHealValue::Calculate()
{
    // The stock answer first: what any healer heals, and what this one heals when no escort runs.
    Unit* stock = PartyMemberToHeal::Calculate();
    // Instances only (2.9.4): the stock value everywhere else.
    if (!CoaGroupTank::GuardScope(bot))
        return stock;

    Unit* escortee = CoaHealEscort::Escortee(bot);
    if (!escortee)
        return stock;

    // The stock candidacy gate: within healDistance * 2 and in sight. A target the healer cannot cast on is never
    // given (the reposition walks it back into range and sight).
    if (bot->GetDistance2d(escortee) >= sPlayerbotAIConfig.healDistance * 2.0f || !bot->IsWithinLOSInMap(escortee))
        return stock;

    // Most hurt wins, as in the stock value; nobody hurt in the group counts as 100%. The heal triggers still gate on
    // the target's health, so a scratched escortee draws no cast, like a scratched member.
    float const stockHp = stock ? stock->GetHealthPct() : 100.0f;
    return escortee->GetHealthPct() < stockHp ? escortee : stock;
}

bool CoaHealRepositionTrigger::IsActive()
{
    if (!CoaGroupTank::GuardScope(bot) || !bot->IsAlive() || !bot->IsInWorld())
        return false;
    CoaHealSettings const& settings = CoaHealSettings::Get();
    // The cheap gates first: this runs every tick.
    if (!settings.healReposition || !PlayerbotAI::IsHeal(bot))
        return false;
    if (!CoaGroupTank::ModuleGuardActs(bot, GuardHealReposition))
        return false;

    CoaHealMath::RepositionInputs in{};
    in.enabled = settings.healReposition;
    in.isHealer = true;
    // The healer that tanks for its group drives the fight; it never walks off to heal.
    in.isReferenceTank = CoaGroupTank::Reference(bot) == bot;
    // Can't move anyway: combat and the trinket handle it.
    in.crowdControlled = bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED |
                                           UNIT_STATE_ROOT);
    if (in.isReferenceTank || in.crowdControlled)
        return false;

    Unit* target = ResolveSameMap(bot, AI_VALUE(ObjectGuid, "coa heal target"));
    in.hasTarget = target != nullptr;
    if (!target)
        return false;

    // Visible heal work first: the stock heal stack does it this tick. Only the case where nothing useful is in sight
    // but an out-of-sight member is dying belongs to this guard.
    Unit* visible = AI_VALUE(Unit*, "party member to heal");
    in.visibleHurtInSight = SameMap(bot, visible) && visible->GetHealthPct() < sPlayerbotAIConfig.mediumHealth &&
                            bot->IsWithinLOSInMap(visible);
    in.inCombat = bot->IsInCombat();
    in.targetInCombat = target->IsInCombat();
    in.targetDistance = bot->GetExactDist2d(target);
    in.maxRange = settings.maxRange;
    in.targetInSight = bot->IsWithinLOSInMap(target);
    in.healRange = botAI->GetRange("heal");
    return CoaHealMath::ShouldReposition(in);
}

bool CoaHealRepositionAction::FindStandoff(Map* map, Unit* target, float radius, float maxRadius, float& x, float& y,
                                           float& z)
{
    float const cx = target->GetPositionX();
    float const cy = target->GetPositionY();
    float const cz = target->GetPositionZ();
    std::vector<CoaHealMath::Point2> const ring =
        CoaHealMath::StandoffRing({ cx, cy }, { bot->GetPositionX(), bot->GetPositionY() }, radius, RingPoints);

    // The first ring point, healer's side first, that a path really reaches, within heal range of the target and in
    // its sight. The path's end is the snapped point: a player's path is clamped onto the mesh, and only an end near
    // the ring point (not on the healer's side of a wall or a floor) counts. Dungeon Clear snapped on the navmesh
    // first and also refused standoffs in its hazards and outside its camp leash (its own data, kept in its runs).
    for (CoaHealMath::Point2 const& point : ring)
    {
        PathGenerator path(bot);
        path.CalculatePath(point.x, point.y, cz, false);
        if (path.GetPathType() != PATHFIND_NORMAL || path.GetPath().empty())
            continue;
        G3D::Vector3 const& end = path.GetPath().back();
        if (!CoaHealMath::PathEndsAt(end.x, end.y, end.z, point.x, point.y, cz, RingSnapFlat, RingSnapHeight))
            continue;
        if (std::hypot(end.x - cx, end.y - cy) > maxRadius)
            continue;
        if (!map->isInLineOfSight(end.x, end.y, end.z + EyeHeight, cx, cy, cz + EyeHeight, bot->GetPhaseMask(),
                                  LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing))
            continue;
        x = end.x;
        y = end.y;
        z = end.z;
        return true;
    }
    return false;
}

bool CoaHealRepositionAction::PathedCloseOn(Unit* target, float stopShort, float& x, float& y, float& z)
{
    PathGenerator path(bot);
    path.CalculatePath(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), false);
    if (path.GetPathType() != PATHFIND_NORMAL)
        return false;
    Movement::PointsArray const& points = path.GetPath();
    if (points.size() < 2)
        return false;
    // Only a route that really ends at the target: an end clamped onto the healer's side of a wall or a floor is a
    // walk into that wall (Karazhan's Maiden hairpin, Dungeon Clear, DcEngageGeometry::PathedCloseOn).
    G3D::Vector3 const& end = points.back();
    if (!CoaHealMath::PathEndsAt(end.x, end.y, end.z, target->GetPositionX(), target->GetPositionY(),
                                 target->GetPositionZ(), LevelTolerance, LevelTolerance))
        return false;
    CoaHealMath::PointShortOfPathEnd(points, stopShort, x, y, z);
    return true;
}

bool CoaHealRepositionAction::Execute(Event /*event*/)
{
    if (!CoaGroupTank::GuardScope(bot))
        return false;
    // Re-read: the target may have healed up or died between the trigger and here.
    Unit* target = ResolveSameMap(bot, AI_VALUE(ObjectGuid, "coa heal target"));
    Map* map = bot->FindMap();
    if (!target || !map)
        return false;

    float const healRange = botAI->GetRange("heal");

    // Already walking to a point that still sees the target in heal range: keep that walk rather than pathing eight
    // ring points again every tick (a re-plotted spline every tick stutters, Dungeon Clear's regroup).
    if (lastValid && bot->isMoving() && target->GetExactDist2d(lastX, lastY) <= healRange &&
        map->isInLineOfSight(lastX, lastY, lastZ + EyeHeight, target->GetPositionX(), target->GetPositionY(),
                             target->GetPositionZ() + EyeHeight, bot->GetPhaseMask(), LINEOFSIGHT_CHECK_VMAP,
                             VMAP::ModelIgnoreFlags::Nothing))
        return true;
    lastValid = false;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool const sampled = FindStandoff(map, target, CoaHealMath::StandoffRadius(healRange), healRange, x, y, z);
    // Nothing on the ring validated (tight geometry): close on the target along its route, rounding the corner is
    // what gives sight back. No route onto the target's floor: hold, and heal what is in sight.
    if (!sampled && !PathedCloseOn(target, CloseOnStopShort, x, y, z))
        return false;

    float const toDest = bot->GetExactDist2d(x, y);
    MovementPriority const priority =
        toDest <= CombatApproachRange ? MovementPriority::MOVEMENT_COMBAT : MovementPriority::MOVEMENT_NORMAL;
    // Own the tick only if the healer really moves: a refused move that still returned true would mute the whole
    // heal rotation for as long as the trigger holds (Dungeon Clear learned it on its combat regroup).
    if (!MoveTo(map->GetId(), x, y, z, false, false, false, false, priority))
        return false;

    lastX = x;
    lastY = y;
    lastZ = z;
    lastValid = true;
    return true;
}

bool CoaTopOffPartyTrigger::IsActive()
{
    if (!CoaGroupTank::GuardScope(bot) || !bot->IsAlive() || !bot->IsInWorld() || bot->IsInCombat())
        return false;
    CoaHealSettings const& settings = CoaHealSettings::Get();
    if (!settings.topOffParty || !PlayerbotAI::IsHeal(bot))
        return false;
    if (!CoaGroupTank::ModuleGuardActs(bot, GuardTopOffParty))
        return false;
    // The CoA kit's heal: a healer of another kit has its own out-of-combat heals.
    if (!context->GetAction("coa heal"))
        return false;

    float const floor = CoaHealMath::TopOffFloor(settings.topOffHealthPct, sPlayerbotAIConfig.almostFullHealth);
    if (floor <= 0.0f)
        return false;

    float const range = sPlayerbotAIConfig.healDistance;
    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !SameMap(bot, member) || member->IsGameMaster())
            continue;
        if (member->GetHealthPct() < floor && bot->GetDistance2d(member) <= range)
            return true;
    }
    return false;
}

void CoaGardesSoinsStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Above the stock "reach party member to heal" (ACTION_CRITICAL_HEAL + 5): that one only knows the members in
    // sight. The trigger defers to any visible heal work, so this never takes a tick a heal could use.
    triggers.push_back(
        new TriggerNode("coa heal reposition", { NextAction("coa heal reposition", ACTION_CRITICAL_HEAL + 6) }));
    // Out of combat: under "coa group member dropping" (ACTION_CRITICAL_HEAL), "coa reach healing range" (- 1) and
    // "coa resurrect" (+ 5), above drinking, eating and the buffs.
    triggers.push_back(new TriggerNode("coa top off party", { NextAction("coa heal", ACTION_CRITICAL_HEAL - 2) }));
}
