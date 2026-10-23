/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OtherSideTriggers.h"
#include "OtherSideShared.h"
#include "Playerbots.h"

using namespace OtherSideShared;

bool OtherSideInHazardTrigger::IsActive()
{
    return InHazard(bot);
}

bool OtherSideEggPhaseTrigger::IsActive()
{
    return InEggPhase(bot);
}

bool OtherSideMuzahsTargetTrigger::IsActive()
{
    if (bot->GetMapId() != OTHER_SIDE_MAP_ID || !bot->HasAura(SPELL_MUZAHS_TARGET))
        return false;
    Unit* muzah = bot->FindNearestCreature(NPC_MUZAH, 80.0f);
    return muzah && !bot->IsWithinMeleeRange(muzah);
}

bool OtherSideSpiderTrigger::IsActive()
{
    if (bot->GetMapId() != OTHER_SIDE_MAP_ID || botAI->IsTank(bot) || botAI->IsHeal(bot))
        return false;
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && current->GetEntry() == NPC_EMBRYO_SPIDER)
        return false;
    return NearestSpider(bot) != nullptr;
}

bool OtherSideEggToKickTrigger::IsActive()
{
    return CachedEggToKick(bot) != nullptr;
}
