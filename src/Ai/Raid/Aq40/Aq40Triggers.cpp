/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Triggers.h"
#include "Aq40Helpers.h"
#include "Playerbots.h"
#include "Aq20Utils.h"
#include "Group.h"

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

bool Aq40SkeramTargetTrigger::IsActive()
{
    if (!IsSkeramEncounter(bot))
        return false;
    Unit* target = ChooseSkeramTarget(bot);
    return target && target != AI_VALUE(Unit*, "current target");
}

bool Aq40SkeramHoldTrigger::IsActive()
{
    // Auto-attack keeps swinging at a veiled Skeram, and at one whose
    // worshipper is too low to take a quarter of the hits.
    if (!IsSkeramEncounter(bot))
        return false;
    Unit* victim = bot->GetVictim();
    if (!IsSkeram(victim))
        return false;
    return victim->HasAura(SPELL_NIGHTMARE_VEIL) || (!PlayerbotAI::IsTank(bot) && ShouldHoldOnSkeram(bot));
}

bool Aq40SkeramPsychicShockTrigger::IsActive()
{
    return IsSkeramEncounter(bot) && botAI->CanMove() && GetPsychicShockSpotNear(bot);
}

bool Aq40SkeramGazeSpreadTrigger::IsActive()
{
    return IsSkeramEncounter(bot) && botAI->CanMove() && ShouldSpreadFromGaze(bot);
}

// ---- CoA Combat (lot 7, docs/AQ-ETAT-3.0.md) ----

Creature* CoaAq40::FindDangerousDancer(Player* bot)
{
    bool const meleeDps = PlayerbotAI::IsMelee(bot) && !PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot);
    std::list<Creature*> dancers;
    bot->GetCreatureListWithEntryInGrid(dancers, NPC_PHANTOM_DANCER, DANCER_DEATH_YARDS + 2.0f);
    for (Creature* dancer : dancers)
    {
        if (!dancer || !dancer->IsAlive())
            continue;
        bool const death = dancer->HasAura(SPELL_DANCE_OF_DEATH);
        if (!death && meleeDps)
            continue;
        float const reach = death ? DANCER_DEATH_YARDS : DANCER_BLADE_YARDS;
        if (bot->GetDistance2d(dancer) < reach)
            return dancer;
    }
    return nullptr;
}

bool Aq40SarturaDancersTrigger::IsActive()
{
    return AqCoaFixesOn() && !PlayerbotAI::IsTank(bot) && CoaAq40::FindDangerousDancer(bot);
}

bool Aq40SarturaMarkTrigger::IsActive()
{
    return AqCoaFixesOn() && RaidKillOrderMarkTrigger::IsActive();
}

std::string CoaAq40::ViscidusFrostSpell(Player* bot)
{
    switch (bot->getClass())
    {
        case CLASS_MAGE:
            return "frostbolt";
        case CLASS_SHAMAN:
            return "frost shock";
        case CLASS_DEATH_KNIGHT:
            return "icy touch";
        default:
            return "";
    }
}

bool CoaAq40::GetHeldEmperor(Player* bot, Unit*& held, Unit*& brother)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;
    AiObjectContext* context = botAI->GetAiObjectContext();
    Unit* nilash = AI_VALUE2(Unit*, "find target", "emperor vek'nilash");
    Unit* lor = AI_VALUE2(Unit*, "find target", "emperor vek'lor");
    if (!nilash || !lor || !nilash->IsAlive() || !lor->IsAlive())
        return false;
    if (nilash->GetVictim() == bot)
    {
        held = nilash;
        brother = lor;
        return true;
    }
    if (lor->GetVictim() == bot)
    {
        held = lor;
        brother = nilash;
        return true;
    }
    return false;
}

Player* CoaAq40::GetDevourPrey(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member && member != bot && member->IsAlive() && member->GetMapId() == bot->GetMapId() &&
            member->HasAura(SPELL_DEVOURS))
            return member;
    }
    return nullptr;
}

bool CoaAq40::IsDevourCompanion(Player* bot, Player* prey)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    uint32 rank = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == prey || !member->IsAlive() || member->GetMapId() != bot->GetMapId() ||
            !GET_PLAYERBOT_AI(member) || PlayerbotAI::IsTank(member) || PlayerbotAI::IsHeal(member) ||
            !PlayerbotAI::IsMelee(member) || InStomach(member))
            continue;
        if (member == bot)
            return rank < DEVOUR_COMPANIONS;
        ++rank;
    }
    return false;
}

bool Aq40ViscidusFrostTrigger::IsActive()
{
    if (!AqCoaFixesOn() || PlayerbotAI::IsTank(bot))
        return false;
    Unit* viscidus = AI_VALUE2(Unit*, "find target", "viscidus");
    if (!viscidus || !viscidus->IsAlive() || viscidus->HasAura(CoaAq40::SPELL_VISCIDUS_FREEZE))
        return false;
    std::string const spell = CoaAq40::ViscidusFrostSpell(bot);
    return !spell.empty() && botAI->CanCastSpell(spell, viscidus);
}

bool Aq40ViscidusShatterTrigger::IsActive()
{
    if (!AqCoaFixesOn() || PlayerbotAI::IsTank(bot) || !PlayerbotAI::IsMelee(bot))
        return false;
    Unit* viscidus = AI_VALUE2(Unit*, "find target", "viscidus");
    return viscidus && viscidus->IsAlive() && viscidus->HasAura(CoaAq40::SPELL_VISCIDUS_FREEZE) &&
           bot->GetVictim() != viscidus;
}

bool Aq40TwinEmperorsApartTrigger::IsActive()
{
    if (!AqCoaFixesOn() || !PlayerbotAI::IsTank(bot))
        return false;
    Unit* held = nullptr;
    Unit* brother = nullptr;
    return CoaAq40::GetHeldEmperor(bot, held, brother) && held->GetExactDist2d(brother) < CoaAq40::TWINS_APART_YARDS;
}

bool Aq40CthunJoinDevourTrigger::IsActive()
{
    if (!AqCoaFixesOn() || !IsCthunEncounter(bot) || !IsCthunPhaseTwo(bot) || InStomach(bot))
        return false;
    Player* prey = CoaAq40::GetDevourPrey(bot);
    return prey && CoaAq40::IsDevourCompanion(bot, prey) && bot->GetExactDist2d(prey) > CoaAq40::DEVOUR_JOIN_YARDS;
}
