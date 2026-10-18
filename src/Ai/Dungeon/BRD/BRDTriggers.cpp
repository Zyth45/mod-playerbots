/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRDTriggers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

#include <list>

using namespace DungeonBoss;

namespace BlackrockDepths
{
    Creature* GoutOver(Player* bot)
    {
        std::list<Creature*> guardians;
        bot->GetCreatureListWithEntryInGrid(guardians, NPC_IRONHAND_GUARDIAN, IRONHAND_SEARCH_RANGE);
        for (Creature* guardian : guardians)
            if (HasAuraNamed(guardian, "Gout of Flame") &&
                InFront(guardian, bot, GOUT_OF_FLAME_REACH, GOUT_OF_FLAME_HALF_ARC))
                return guardian;
        return nullptr;
    }

    Creature* BurningSpiritToKill(PlayerbotAI* botAI, AiObjectContext* context, Player* bot)
    {
        Unit* flamelash = AI_VALUE2(Unit*, "find target", "ambassador flamelash");
        if (!flamelash || !flamelash->IsAlive())
            return nullptr;

        std::list<Creature*> spirits;
        flamelash->GetCreatureListWithEntryInGrid(spirits, NPC_BURNING_SPIRIT, BURNING_SPIRIT_SEARCH_RANGE);

        Creature* nearest = nullptr;
        for (Creature* spirit : spirits)
        {
            if (!spirit->IsAlive() || !bot->IsValidAttackTarget(spirit))
                continue;
            if (!nearest || flamelash->GetExactDist2d(spirit) < flamelash->GetExactDist2d(nearest))
                nearest = spirit;
        }
        return nearest;
    }

    Unit* ThaurissanToFocus(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* thaurissan = AI_VALUE2(Unit*, "find target", "emperor dagran thaurissan");
        Unit* moira = AI_VALUE2(Unit*, "find target", "princess moira bronzebeard");
        if (!thaurissan || !moira || !thaurissan->IsAlive() || !moira->IsAlive())
            return nullptr;

        return thaurissan->isTargetableForAttack() ? thaurissan : nullptr;
    }

    Unit* GroundTremorNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot)
    {
        for (char const* name : GROUND_TREMOR_BOSSES)
        {
            Unit* boss = AI_VALUE2(Unit*, "find target", name);
            if (boss && boss->IsAlive() && Within(bot, boss, GROUND_TREMOR_RADIUS))
                return boss;
        }
        return nullptr;
    }
}

using namespace BlackrockDepths;

bool BrdIronhandGoutTrigger::IsActive()
{
    return bot->IsInCombat() && GoutOver(bot) != nullptr;
}

bool BrdBurningSpiritTrigger::IsActive()
{
    if (!botAI->IsRangedDps(bot))
        return false;

    Creature* spirit = BurningSpiritToKill(botAI, context, bot);
    return spirit && AI_VALUE(Unit*, "current target") != spirit;
}

bool BrdThaurissanFocusTrigger::IsActive()
{
    if (botAI->IsTank(bot) || botAI->IsHeal(bot))
        return false;

    Unit* thaurissan = ThaurissanToFocus(botAI, context);
    return thaurissan && AI_VALUE(Unit*, "current target") != thaurissan;
}

bool BrdGroundTremorTrigger::IsActive()
{
    return !botAI->IsTank(bot) && botAI->IsRanged(bot) && GroundTremorNear(botAI, context, bot);
}

bool BrdFrontalConeTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    FrontalCone const* cone = nullptr;
    return ConeOver(botAI, context, bot, FRONTAL_CONES, cone) != nullptr;
}
