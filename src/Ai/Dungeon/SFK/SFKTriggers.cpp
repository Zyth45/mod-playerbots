/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SFKTriggers.h"
#include "AiObjectContext.h"
#include "Playerbots.h"

namespace NandosPack
{
    Unit* Nearest(PlayerbotAI* botAI, Player* bot, AiObjectContext* context)
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", "wolf master nandos");
        if (!boss)
            return nullptr;

        // The Horror first: it summons Lupine Delusions while it lives. A Delusion that is hitting
        // one of us next: it vanishes on the first hit it takes, but only when somebody hits it, and
        // left alone it beats on a healer for as long as it lives. The Bleak Worg after that, whose
        // Wavering Will slows the attacks and casts of whoever it hits.
        GuidVector targets = AI_VALUE(GuidVector, "possible targets no los");
        for (uint32 entry : { NPC_LUPINE_HORROR, NPC_LUPINE_DELUSION, NPC_BLEAK_WORG, NPC_SLAVERING_WORG })
        {
            for (auto& guid : targets)
            {
                Unit* unit = botAI->GetUnit(guid);
                if (!unit || unit->GetEntry() != entry || !unit->IsAlive() || !unit->isTargetableForAttack())
                    continue;

                if (entry == NPC_LUPINE_DELUSION)
                {
                    Unit* victim = unit->GetVictim();
                    if (!victim || !victim->IsPlayer() || !bot->IsInSameGroupWith(victim->ToPlayer()))
                        continue;
                }

                if (unit->GetExactDist(boss) <= SFK_NANDOS_PACK_RANGE)
                    return unit;
            }
        }

        return nullptr;
    }
}

bool NandosPackTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    return NandosPack::Nearest(botAI, bot, context) != nullptr;
}

bool ArugalThundershockTrigger::IsActive()
{
    if (!botAI->IsRanged(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", "archmage arugal");
    if (!boss || !boss->IsAlive())
        return false;

    return bot->GetExactDist2d(boss->GetPosition()) < THUNDERSHOCK_RADIUS + THUNDERSHOCK_SAFETY;
}
