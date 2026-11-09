/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ZGTriggers.h"
#include "Playerbots.h"
#include "ZGHelpers.h"
#include "MovementActions.h"

using namespace ZulGurubHelpers;

bool BloodlordMandokirThreateningGazeTrigger::IsActiveInEncounter()
{
    return (bot->GetVictim() || bot->IsNonMeleeSpellCast(false)) && IsGazedByMandokir(bot);
}

bool HakkarBloodScytheTrigger::IsActiveInEncounter()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "hakkar");
    if (!boss || !boss->IsAlive() || boss->GetVictim() == bot)
        return false;

    return bot->GetDistance2d(boss) < HAKKAR_SCYTHE_DANGER_RANGE && boss->HasInArc(HAKKAR_SCYTHE_DANGER_ARC, bot);
}

bool VenoxisVenomSpitTrigger::IsActiveInEncounter()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", "high priest venoxis");
    if (!boss || !boss->IsAlive() || boss->GetVictim() == bot ||
        !boss->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_VENOXIS_SNAKE_FORM)))
        return false;

    return bot->GetDistance2d(boss) < VENOXIS_SPIT_DANGER_RANGE && boss->HasInArc(VENOXIS_SPIT_DANGER_ARC, bot);
}

bool HakkarBloodPoolTrigger::IsActiveInEncounter()
{
    // CoA Combat (HK2): during Blood Siphon the poison pool is the counter, not a danger.
    if (ZgCoaFixesOn() && CoaHakkarSiphon(bot))
        return bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_CORRUPTED_BLOOD));
    return bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_CORRUPTED_BLOOD)) ||
           bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_POISONED_BLOOD));
}

// ---- CoA Combat (lot 7, docs/ZG-ETAT-3.0.md) ----

bool HakkarSiphonPoolTrigger::IsActiveInEncounter()
{
    return ZgCoaFixesOn() && CoaHakkarSiphon(bot) && LacksPoisonousBlood(bot) && FindPoisonPool(bot, 60.0f);
}

bool GrilekPursuitTrigger::IsActiveInEncounter()
{
    if (!ZgCoaFixesOn() || !bot->HasAura(SPELL_GRILEK_PURSUIT))
        return false;
    Unit* boss = AI_VALUE2(Unit*, "find target", "gri'lek");
    return boss && boss->IsAlive() && bot->GetDistance2d(boss) < GRILEK_PREY_KEEP_YARDS;
}
