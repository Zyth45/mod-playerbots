/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ZGTriggers.h"
#include "Playerbots.h"
#include "ZGHelpers.h"

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

bool HakkarBloodPoolTrigger::IsActiveInEncounter()
{
    return bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_CORRUPTED_BLOOD)) ||
           bot->HasAura(static_cast<uint32>(ZulGurubSpells::SPELL_POOL_OF_POISONED_BLOOD));
}
