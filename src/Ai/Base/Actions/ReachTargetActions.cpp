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
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ServerFacade.h"

static constexpr float GROUP_SIGHT_SPOT_DISTANCE = 3.0f;

bool ReachTargetAction::Execute(Event /*event*/)
{
    Unit* target = AI_VALUE(Unit*, GetTargetName());
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
