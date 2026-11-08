/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "../CoaGardesCombat.h"

#include "../Brd/CoaIronHallDecision.h"
#include "../Common/CoaGardesGate.h"
#include "../Common/CoaGardesRunHooks.h"
#include "../Common/CoaGardesSettings.h"
#include "../Common/CoaGardesUnits.h"
#include "CoaRegroupDecision.h"
#include "CoaStandoffMath.h"

#include "CombatManager.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "Timer.h"

#include <cmath>
#include <unordered_set>

// The contribution-gated combat regroup, from Dungeon Clear (DungeonClearRegroupCombat{Trigger,Action}). The trigger
// gathers the live reads, asks the pure verdict (CoaRegroupDecision) and layers the debounce / latch / cooldown on it;
// the action walks the bot to a role-correct standoff with LOS on the fight, never onto the tank's cell.
namespace
{
    // Someone of the group (the bot aside) is under the heal floor, LOS-blind: the heal reposition owns that case, the
    // regroup stands down for a healer then. Dungeon Clear's `dungeon clear heal target` is non-empty exactly then
    // (its tank bias only orders the candidates).
    bool HasHurtHealTarget(Player* bot, float hpFloor)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        float const maxDist = sPlayerbotAIConfig.healDistance * 2.0f;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsInWorld() || !member->IsAlive() || member->IsGameMaster())
                continue;
            if (member->FindMap() != bot->FindMap() || member->IsCharmed())
                continue;
            if (bot->GetDistance2d(member) > maxDist)
                continue;
            if (member->GetHealthPct() < hpFloor)
                return true;
        }
        return false;
    }

    // Reachable on its level: the same level at once, else a ground route that ends on the unit's level.
    bool LevelReachable(Player* bot, Unit* u)
    {
        if (std::fabs(u->GetPositionZ() - bot->GetPositionZ()) <= CoaStandoffMath::Z_LEVEL_TOLERANCE)
            return true;
        PathGenerator gen(bot);
        gen.CalculatePath(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(), false);
        if (gen.GetPathType() != PATHFIND_NORMAL || gen.GetPath().empty())
            return false;
        return std::fabs(gen.GetPath().back().z - u->GetPositionZ()) <= CoaStandoffMath::Z_LEVEL_TOLERANCE;
    }

    // The nearest live unit holding the tank in combat that the bot can fight (Dungeon Clear's LeaderFightAnchor):
    // its combat references, its attackers, its victim. LOS-blind on purpose. None: the tank is the anchor.
    Unit* FightAnchor(Player* bot, Player* tank, Position& anchorPos)
    {
        std::vector<Unit*> holders;
        std::unordered_set<uint64> seen;
        auto collect = [&](Unit* u)
        {
            if (u && seen.insert(u->GetGUID().GetRawValue()).second)
                holders.push_back(u);
        };
        for (auto const& kv : tank->GetCombatManager().GetPvECombatRefs())
            if (CombatReference* const ref = kv.second)
                collect(ref->GetOther(tank));
        for (Unit* const attacker : tank->getAttackers())
            collect(attacker);
        collect(tank->GetVictim());

        Unit* best = nullptr;
        float bestDist = 0.0f;
        for (Unit* a : holders)
        {
            if (!a->IsAlive() || a->FindMap() != bot->FindMap() || !bot->IsValidAttackTarget(a))
                continue;
            float const d = bot->GetExactDist2d(a);
            if (best && d >= bestDist)
                continue;
            if (!CoaStandoffMath::IsRegroupAnchorCandidate(bot->GetExactDist(a), CoaStandoffMath::ENGAGEMENT_RADIUS,
                                                           LevelReachable(bot, a)))
                continue;
            best = a;
            bestDist = d;
        }
        anchorPos = best ? best->GetPosition() : tank->GetPosition();
        return best;
    }

    // A point `stopShort` yards before the end of the ground route to `target`, when the route really ends there
    // (Dungeon Clear's PathedCloseOn): a straight line through a wall is not a walk.
    bool PathedCloseOn(Player* bot, Position const& target, float stopShort, Position& out)
    {
        PathGenerator gen(bot);
        gen.CalculatePath(target.GetPositionX(), target.GetPositionY(), target.GetPositionZ(), false);
        if (gen.GetPathType() != PATHFIND_NORMAL)
            return false;
        Movement::PointsArray const& path = gen.GetPath();
        if (path.size() < 2)
            return false;
        G3D::Vector3 const& end = path.back();
        if (std::fabs(end.z - target.GetPositionZ()) > CoaStandoffMath::Z_LEVEL_TOLERANCE ||
            std::hypot(end.x - target.GetPositionX(), end.y - target.GetPositionY()) > CoaStandoffMath::Z_LEVEL_TOLERANCE)
            return false;
        std::vector<CoaStandoffMath::Point> points;
        points.reserve(path.size());
        for (G3D::Vector3 const& p : path)
            points.push_back({p.x, p.y, p.z});
        CoaStandoffMath::Point const p = CoaStandoffMath::PointShortOfPathEnd(points, stopShort);
        out = Position(p.x, p.y, p.z, target.GetOrientation());
        return true;
    }
}

bool CoaCombatRegroupTrigger::IsActive()
{
    // Every early-out stands the latch down: a latch left set would skip the debounce on the next fight's first tick.
    if (!botAI || !CoaGroupTank::GuardScope(bot) || !CoaGroupTank::ModuleGuardActs(bot, CoaGardesGate::COMBAT_REGROUP))
    {
        CoaRegroupDecision::StandDown(latch);
        return false;
    }

    CoaGardesSettings const& settings = CoaGardesSettings::Get();
    Player* tank = CoaGardesUnits::Tank(bot);

    CoaRegroupDecision::GateInputs g;
    g.botAlive = bot->IsAlive();
    g.inCombat = bot->IsInCombat();
    g.featureOn = settings.combatRegroup;
    g.hasTank = tank != nullptr;
    g.tankIsSelf = tank == bot;
    g.tankSameMap = tank && tank->FindMap() == bot->FindMap();
    g.tankIsHuman = CoaGardesUnits::IsHuman(tank);
    g.botIsTank = PlayerbotAI::IsTank(bot);
    // Only asked when everything else holds: the hook belongs to a Dungeon Clear run.
    g.campHoldPassive = false;
    if (!CoaRegroupDecision::GateOpen(g) ||
        (CoaGroupTank::InDungeonClearRun(bot) && CoaGardesRunHooks::IsCampHoldPassive(bot)))
    {
        CoaRegroupDecision::StandDown(latch);
        return false;
    }

    CoaRegroupDecision::RegroupInputs in;
    in.isHealer = PlayerbotAI::IsHeal(bot);
    in.isMelee = PlayerbotAI::IsMelee(bot);
    in.casting = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) != nullptr ||
                 bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr;
    in.ccd = CoaGardesUnits::Controlled(bot);
    in.hasVisibleAttacker = !context->GetValue<GuidVector>("attackers")->Get().empty();
    in.hasLosTarget = false;
    if (!in.isHealer)
        if (Unit* cur = context->GetValue<Unit*>("current target")->Get())
            in.hasLosTarget = cur->IsAlive() && cur->FindMap() == bot->FindMap() && bot->IsValidAttackTarget(cur) &&
                              bot->IsWithinLOSInMap(cur);
    in.hasHurtHealTarget = in.isHealer && HasHurtHealTarget(bot, settings.healRepositionHpFloor);
    in.tankLos = bot->IsWithinLOSInMap(tank);
    in.tankDist2d = bot->GetExactDist2d(tank);
    in.healRange = botAI->GetRange("heal");
    in.hardTether = settings.combatRegroupDistance;
    in.slack = settings.combatRegroupSlack;

    CoaRegroupDecision::RegroupVerdict const verdict = CoaRegroupDecision::DecideCombatRegroup(in);
    bool const wasLatched = latch.latched;
    bool const fire = CoaRegroupDecision::Step(latch, verdict, getMSTime(),
                                               uint32(settings.combatRegroupCooldown * 1000.0f));
    if (fire && !wasLatched)
        LOG_DEBUG("playerbots", "[coa gardes:{}] combat regroup: {} ({:.1f}yd, los={}, healRange={:.1f})",
                  bot->GetName(),
                  verdict == CoaRegroupDecision::RegroupVerdict::HardTether ? "hard tether"
                  : in.isHealer                                             ? "healer pre-position"
                                                                            : "dps without attackers",
                  in.tankDist2d, in.tankLos ? 1 : 0, in.healRange);
    return fire;
}

bool CoaCombatRegroupAction::FindStandoffPoint(Position const& center, float ringRadius, float maxRadius, float& x,
                                               float& y, float& z)
{
    Map* map = bot->GetMap();
    if (!map)
        return false;
    float const cx = center.GetPositionX();
    float const cy = center.GetPositionY();
    float const cz = center.GetPositionZ();
    std::vector<CoaStandoffMath::Point> const cands = CoaStandoffMath::StandoffCandidates(
        {cx, cy, cz}, {bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()}, ringRadius, 7);
    std::vector<CoaGround> const ground = CoaHostileGround(bot);
    bool const ironHall = bot->GetMapId() == CoaBrd::MAP_ID;

    constexpr float EYE = 2.0f;
    constexpr float SNAP = 8.0f;
    for (CoaStandoffMath::Point const& c : cands)
    {
        float const gz = map->GetHeight(bot->GetPhaseMask(), c.x, c.y, cz + EYE, true);
        if (gz == INVALID_HEIGHT || std::fabs(gz - cz) > SNAP)
            continue;
        if (std::hypot(c.x - cx, c.y - cy) > maxRadius)
            continue;
        // Never a standoff somewhere the bot will be burnt standing: an enemy ground effect, the Iron Hall's fire.
        if (CoaInGround(ground, c.x, c.y))
            continue;
        if (ironHall && CoaIronHall::InHall(c.x, c.y, gz) && CoaIronHall::InFire(c.x, c.y))
            continue;
        if (!map->isInLineOfSight(c.x, c.y, gz + EYE, cx, cy, cz + EYE, bot->GetPhaseMask(), LINEOFSIGHT_CHECK_VMAP,
                                  VMAP::ModelIgnoreFlags::Nothing))
            continue;
        PathGenerator gen(bot);
        gen.CalculatePath(c.x, c.y, gz, false);
        if (gen.GetPathType() != PATHFIND_NORMAL)
            continue;
        x = c.x;
        y = c.y;
        z = gz;
        return true;
    }
    return false;
}

bool CoaCombatRegroupAction::Execute(Event /*event*/)
{
    if (!CoaGroupTank::GuardScope(bot))
        return false;
    Player* tank = CoaGardesUnits::Tank(bot);
    if (!tank || tank == bot)
        return false;

    bool const isHealer = PlayerbotAI::IsHeal(bot);
    bool const isMelee = PlayerbotAI::IsMelee(bot);

    // A healer pre-positions on the tank; a DPS anchors on the fight the tank holds, to regain LOS on a target.
    Position anchorPos;
    Unit* anchorUnit = nullptr;
    if (isHealer)
        anchorPos = tank->GetPosition();
    else
        anchorUnit = FightAnchor(bot, tank, anchorPos);

    // Role-range ring. Ranged DPS: 0.8x spell range. Healer: max(5, 0.6x heal range), the band the heal reposition
    // parks a healer in. Melee has no ring: it closes on the anchor.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool sampled = false;
    if (!isMelee)
    {
        float const range = botAI->GetRange(isHealer ? "heal" : "spell");
        float const ring = isHealer ? std::max(5.0f, range * 0.6f) : range * 0.8f;
        sampled = FindStandoffPoint(anchorPos, ring, range, x, y, z);
    }

    // Fallback (no ring point, or a melee): close on the anchor ALONG THE ROUTE, stopping attack range short. No route
    // onto the anchor's floor: hold.
    if (!sampled)
    {
        float const attackRange =
            isMelee ? (bot->GetCombatReach() + (anchorUnit ? anchorUnit->GetCombatReach() : 0.0f) + 1.0f)
                    : std::max(5.0f, botAI->GetRange("spell") - CONTACT_DISTANCE);
        Position close;
        if (!PathedCloseOn(bot, anchorPos, attackRange, close))
            return false;
        x = close.GetPositionX();
        y = close.GetPositionY();
        z = close.GetPositionZ();
    }

    // A reconnect spline already in flight toward the same point owns the tick without re-plotting it.
    if (bot->isMoving() && lastDestValid && lastDest.GetExactDist2d(x, y) < 3.0f)
        return true;

    // Combat priority only for the final approach: a long leg at normal priority lets the bot break off and fight
    // what it aggroes on the way instead of plowing through.
    float const toDest = bot->GetExactDist2d(x, y);
    MovementPriority const prio = toDest <= CoaStandoffMath::COMBAT_APPROACH_RANGE ? MovementPriority::MOVEMENT_COMBAT
                                                                                    : MovementPriority::MOVEMENT_NORMAL;

    // Own the tick only when a move was issued: the rung outranks reach spell and the rotation, so a refused move that
    // still returned true would mute the bot.
    if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, prio))
        return false;
    lastDest = Position(x, y, z, 0.0f);
    lastDestValid = true;
    return true;
}
