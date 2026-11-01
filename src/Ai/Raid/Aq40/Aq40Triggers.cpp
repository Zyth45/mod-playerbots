/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Triggers.h"
#include "Aq40Helpers.h"
#include "Playerbots.h"

using namespace Aq40Helpers;

namespace
{
// A bot this close to its spot does not walk back to it.
constexpr float SPOT_TOLERANCE = 4.0f;
}  // namespace

bool Aq40CthunLeaveStomachTrigger::IsActive()
{
    return IsCthunEncounter(bot) && ShouldLeaveStomach(bot);
}

bool Aq40CthunDarkGlareTrigger::IsActive()
{
    if (!IsCthunEncounter(bot) || InStomach(bot))
        return false;
    Creature* eye = GetEye(bot);
    return eye && eye->GetHealth() > 0 && eye->HasAura(SPELL_RED_COLORATION) && IsInGlarePath(bot, eye);
}

bool Aq40CthunEldritchTrigger::IsActive()
{
    if (!IsCthunEncounter(bot))
        return false;
    Unit* caster = GetEldritchCaster(bot);
    // Revelation stuns whoever has the caster within 90 degrees in front.
    return caster && bot->isInFront(caster, float(M_PI) / 2.0f);
}

bool Aq40CthunDevourTrigger::IsActive()
{
    return IsCthunEncounter(bot) && IsCthunPhaseTwo(bot) && GetDevourPreyNear(bot);
}

bool Aq40CthunCenterTrigger::IsActive()
{
    // A player on C'Thun's center is spat out by area trigger 4036, which only
    // a client fires. The stomach exit drops players right there.
    return IsCthunEncounter(bot) && IsCthunPhaseTwo(bot) && !InStomach(bot) && botAI->CanMove() &&
           bot->GetExactDist2d(CTHUN_CENTER.GetPositionX(), CTHUN_CENTER.GetPositionY()) < CTHUN_SPIT_OUT_RADIUS;
}

bool Aq40CthunPositionTrigger::IsActive()
{
    if (!IsCthunEncounter(bot) || InStomach(bot))
        return false;
    Position spot;
    if (!GetCthunSpot(bot, spot) || bot->GetExactDist2d(&spot) <= SPOT_TOLERANCE)
        return false;

    // Melee may leave their spot for a unit close to it (ChooseCthunTarget);
    // do not pull them back while they fight it.
    if (PlayerbotAI::IsMelee(bot) || PlayerbotAI::IsTank(bot))
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        if (target && target->IsAlive() && !IsCthunBody(target) && target->GetEntry() != NPC_EYE_OF_CTHUN &&
            target->GetExactDist2d(&spot) <= CTHUN_FAR_CLAW + target->GetCombatReach())
            return false;
    }
    return true;
}

bool Aq40CthunTargetTrigger::IsActive()
{
    if (!IsCthunEncounter(bot))
        return false;
    Unit* target = ChooseCthunTarget(bot);
    return target && target != AI_VALUE(Unit*, "current target");
}
