/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ReachTargetActions.h"
#include "Event.h"
#include "Formations.h"
#include "Group.h"
#include "GroupReference.h"
#include "PathGenerator.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include <cmath>
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "GridNotifiersImpl.h"
#include "GridNotifiers.h"
#include "DynamicObject.h"
#include "CellImpl.h"

static constexpr float GROUP_SIGHT_SPOT_DISTANCE = 3.0f;


bool ReachTargetAction::Execute(Event /*event*/)
{
    Unit* target = AI_VALUE(Unit*, GetTargetName());

    // A point next to the enemy that is not in a ground effect: the one nearest the straight way in, else, with the enemy
    // standing in it, a damage dealer waits at its edge (the tank still goes in).
    if (target && bot->IsValidAttackTarget(target) && bot->GetGroup() && !botAI->IsTank(bot))
    {
        std::vector<CoaGround> const zones = CoaHostileGround(bot);
        if (!zones.empty() && !CoaInGround(zones, bot->GetPositionX(), bot->GetPositionY(), 0.0f))
        {
            float const reach = std::max(distance - 1.0f, 1.0f);
            float const base = target->GetAngle(bot);
            float const straightX = target->GetPositionX() + reach * std::cos(base);
            float const straightY = target->GetPositionY() + reach * std::sin(base);
            if (CoaInGround(zones, straightX, straightY))
            {
                for (float step : { 0.4f, -0.4f, 0.8f, -0.8f, 1.2f, -1.2f, 1.6f, -1.6f, 2.0f, -2.0f, 2.4f, -2.4f, float(M_PI) })
                {
                    float x = target->GetPositionX() + reach * std::cos(base + step);
                    float y = target->GetPositionY() + reach * std::sin(base + step);
                    float z = target->GetPositionZ();
                    if (CoaInGround(zones, x, y))
                        continue;
                    bot->UpdateAllowedPositionZ(x, y, z);
                    if (!target->IsWithinLOS(x, y, z + 2.0f))
                        continue;
                    if (bot->GetExactDist2d(x, y) <= 1.5f)
                        return false;
                    return MoveTo(bot->GetMapId(), x, y, z, false, false, false, true, MovementPriority::MOVEMENT_COMBAT, true);
                }
                return false;
            }
        }
    }
    // Heal and resurrect reaches use this class too. Only hostile targets get the stack.
    if (target && bot->IsValidAttackTarget(target) && bot->GetGroup() && !botAI->IsTank(bot))
    {
        bool holdBack = PlayerbotAI::IsRanged(bot) || botAI->IsHeal(bot);
        WorldLocation spot;
        if (holdBack && GetBehindTankNearHealerLocation(botAI, bot, spot, target))
        {
            float dx = spot.GetPositionX() - target->GetPositionX();
            float dy = spot.GetPositionY() - target->GetPositionY();
            float dz = spot.GetPositionZ() - target->GetPositionZ();
            float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            float destX = spot.GetPositionX();
            float destY = spot.GetPositionY();
            float destZ = spot.GetPositionZ();
            if (len > distance && len > 0.5f)
            {
                float scale = distance / len;
                destX = target->GetPositionX() + dx * scale;
                destY = target->GetPositionY() + dy * scale;
                destZ = target->GetPositionZ() + dz * scale;
            }

            // The stack spot is behind the tank's victim. If clamping toward spell range
            // crossed onto the mob side of that spot, stay on the stack instead of looping.
            float destToMob = target->GetExactDist2d(destX, destY);
            if (destToMob + 1.0f < spot.GetExactDist2d(target))
            {
                destX = spot.GetPositionX();
                destY = spot.GetPositionY();
                destZ = spot.GetPositionZ();
            }

            bot->UpdateAllowedPositionZ(destX, destY, destZ);

            if (bot->GetExactDist(destX, destY, destZ) <= 2.5f)
                return false;

            return MoveTo(bot->GetMapId(), destX, destY, destZ, false, false, false, true,
                          MovementPriority::MOVEMENT_COMBAT, true);
        }

        if (!holdBack && GetTankSideMeleeLocation(botAI, bot, target, distance, spot))
        {
            if (bot->IsWithinCombatRange(target, distance))
                return false;

            return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false, false,
                          false, true, MovementPriority::MOVEMENT_COMBAT, true);
        }
    }

    return ReachCombatTo(target, distance);
}

bool ReachTargetAction::isUseful()
{
    // do not move while staying
    if (botAI->HasStrategy("stay", botAI->GetState()))
    {
        return false;
    }

    // do not move while casting
    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr)
    {
        return false;
    }
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (!bot->IsWithinCombatRange(target, distance))
        return !(ChasesEnemy() && IsChaseLeashed(target));

    return sPlayerbotAIConfig.coaHoldBehindTank && bot->IsValidAttackTarget(target) &&
           (PlayerbotAI::IsRanged(bot) || botAI->IsHeal(bot)) && BotIsInFrontOfLivingTank(botAI, bot, target);
}

Unit* ReachTargetAction::ChaseAnchor() const
{
    if (!PlayerbotAI::IsTank(bot))
        return AI_VALUE(Unit*, "main tank");

    Unit* healer = nullptr;
    Unit* nearest = nullptr;
    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            continue;

        if (PlayerbotAI::IsHeal(member) && (!healer || bot->GetExactDist(member) < bot->GetExactDist(healer)))
            healer = member;
        if (!nearest || bot->GetExactDist(member) < bot->GetExactDist(nearest))
            nearest = member;
    }

    return healer ? healer : nearest;
}

bool ReachTargetAction::IsChaseLeashed(Unit* target) const
{
    if (!sPlayerbotAIConfig.coaChaseLeash || !target || !bot->GetGroup() || !bot->IsInCombat() ||
        !bot->GetMap()->IsDungeon())
        return false;

    if (PlayerbotAI::IsTank(bot))
    {
        // a tank always takes back an enemy hitting one of its group, however far it stands from the healer
        Unit* victim = target->GetVictim();
        Player* hit = victim ? victim->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!victim || (hit && bot->IsInSameGroupWith(hit)))
            return false;
    }

    Unit* anchor = ChaseAnchor();
    if (!anchor || anchor == bot || !anchor->IsAlive() || anchor->GetMapId() != bot->GetMapId())
        return false;

    return anchor->GetExactDist(target) > botAI->GetRange("heal");
}

std::string const ReachTargetAction::GetTargetName() { return "current target"; }

bool CastReachTargetSpellAction::isUseful()
{
    // do not move while staying
    if (botAI->HasStrategy("stay", botAI->GetState()))
    {
        return false;
    }

    return ServerFacade::instance().IsDistanceGreaterThan(AI_VALUE2(float, "distance", "current target"),
                                                (distance + sPlayerbotAIConfig.contactDistance));
}

ReachSpellAction::ReachSpellAction(PlayerbotAI* botAI)
    : ReachTargetAction(botAI, "reach spell", botAI->GetRange("spell"))
{
}

bool ReachLineOfSightAction::Execute(Event /*event*/)
{
    Unit* target = GetTarget();
    if (!target || !IsMovingAllowed(target))
        return false;

    if (!PlayerbotAI::IsRanged(bot) || !bot->GetGroup())
        return MoveToLOS(target, PlayerbotAI::IsRanged(bot));

    // Walking towards the enemy can pull idle packs, a spot held by the group cannot
    Player* member = FindGroupMemberInSightOf(target);
    if (!member)
        return false;

    return MoveNear(member, GROUP_SIGHT_SPOT_DISTANCE, MovementPriority::MOVEMENT_COMBAT);
}

Player* ReachLineOfSightAction::FindGroupMemberInSightOf(Unit* target)
{
    Player* best = nullptr;
    int bestRank = 3;
    float bestDistance = sPlayerbotAIConfig.sightDistance;
    float range = botAI->GetRange("spell");

    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->IsCharmed() ||
            member->GetMapId() != bot->GetMapId())
            continue;

        float distance = bot->GetExactDist(member);
        if (distance <= GROUP_SIGHT_SPOT_DISTANCE || distance >= sPlayerbotAIConfig.sightDistance)
            continue;

        if (!member->IsWithinCombatRange(target, range) || !member->IsWithinLOSInMap(target))
            continue;

        // Step to the healer, then the tank. Anyone else is a last resort.
        int rank = 2;
        if (botAI->IsHeal(member))
            rank = 0;
        else if (botAI->IsTank(member))
            rank = 1;

        if (rank < bestRank || (rank == bestRank && distance < bestDistance))
        {
            best = member;
            bestRank = rank;
            bestDistance = distance;
        }
    }

    return best;
}

bool ReachLineOfSightAction::isUseful()
{
    if (botAI->HasStrategy("stay", botAI->GetState()))
        return false;

    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr)
        return false;

    Unit* target = GetTarget();
    return target && !bot->IsWithinLOSInMap(target);
}

std::string const ReachLineOfSightAction::GetTargetName() { return "current target"; }

bool RegainGroupSightAction::isUseful()
{
    if (botAI->HasStrategy("stay", botAI->GetState()))
        return false;

    Unit* tank = AI_VALUE(Unit*, "main tank");
    return tank && tank != bot && tank->IsAlive() && !bot->IsWithinLOSInMap(tank);
}

Player* RegainGroupSightAction::FindMemberInSightOf(Unit* tank)
{
    Player* best = nullptr;
    int bestRank = 3;
    float bestDistance = sPlayerbotAIConfig.sightDistance;
    float const range = botAI->GetRange("heal");

    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsAlive() || member->IsCharmed() ||
            member->GetMapId() != bot->GetMapId() || !member->IsWithinLOSInMap(tank) ||
            member->GetExactDist(tank) > range)
            continue;

        // The healer and the ranged stand where the group means to stand; the tank itself is in the melee.
        int rank = 1;
        if (botAI->IsHeal(member))
            rank = 0;
        else if (member == tank)
            rank = 2;

        float const distance = bot->GetExactDist(member);
        if (rank < bestRank || (rank == bestRank && distance < bestDistance))
        {
            best = member;
            bestRank = rank;
            bestDistance = distance;
        }
    }

    return best;
}

bool RegainGroupSightAction::Execute(Event /*event*/)
{
    Unit* tank = AI_VALUE(Unit*, "main tank");
    if (!tank || !bot->GetGroup() || !IsMovingAllowed())
        return false;

    Player* member = FindMemberInSightOf(tank);
    if (!member)
        return false;

    // A bot standing inside the geometry finds no path at all; it is never ported out, it waits for the fight or
    // for its master to move it.
    PathGenerator path(bot);
    path.CalculatePath(member->GetPositionX(), member->GetPositionY(), member->GetPositionZ(), false);
    PathType const type = path.GetPathType();
    if (type != PATHFIND_NORMAL && type != PATHFIND_INCOMPLETE)
        return false;

    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);
    return MoveNear(member, GROUP_SIGHT_SPOT_DISTANCE, MovementPriority::MOVEMENT_COMBAT);
}

ReachPartyMemberToHealAction::ReachPartyMemberToHealAction(PlayerbotAI* botAI)
    : ReachTargetAction(botAI, "reach party member to heal", botAI->GetRange("heal"))
{
}

bool ReachPartyMemberToHealAction::Execute(Event event)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (sPlayerbotAIConfig.regainLineOfSight && bot->IsWithinCombatRange(target, distance) &&
        !bot->IsWithinLOSInMap(target))
        return IsMovingAllowed(target) && MoveToLOS(target, true);

    return ReachTargetAction::Execute(event);
}

bool ReachPartyMemberToHealAction::isUseful()
{
    if (ReachTargetAction::isUseful())
        return true;

    if (!sPlayerbotAIConfig.regainLineOfSight)
        return false;

    if (botAI->HasStrategy("stay", botAI->GetState()))
        return false;

    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr)
        return false;

    Unit* target = GetTarget();
    return target && !bot->IsWithinLOSInMap(target);
}

std::string const ReachPartyMemberToHealAction::GetTargetName() { return "party member to heal"; }

ReachPartyMemberToResurrectAction::ReachPartyMemberToResurrectAction(PlayerbotAI* botAI)
    : ReachTargetAction(botAI, "reach party member to resurrect", botAI->GetRange("spell"))
{
}

std::string const ReachPartyMemberToResurrectAction::GetTargetName() { return "party member to resurrect"; }
