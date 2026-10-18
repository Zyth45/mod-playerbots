/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StratTriggers.h"

#include "AiObjectContext.h"
#include "Group.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace Stratholme
{
    Unit* DathrohanNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot)
    {
        Unit* dathrohan = AI_VALUE2(Unit*, "find target", "grand crusader dathrohan");
        return dathrohan && dathrohan->IsAlive() && Within(bot, dathrohan, CRUSADERS_HAMMER_RADIUS) ? dathrohan
                                                                                                     : nullptr;
    }

    Player* RetributionNear(PlayerbotAI* botAI, AiObjectContext* context, Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group || !AI_VALUE2(Unit*, "find target", "timmy the cruel"))
            return nullptr;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;

            int32 const remaining = AuraRemainingNamed(member, "Divine Retribution");
            if (remaining >= 0 && remaining < DIVINE_RETRIBUTION_WARNING_MS &&
                Within(bot, member, DIVINE_RETRIBUTION_RADIUS))
                return member;
        }
        return nullptr;
    }
}

using namespace Stratholme;

bool StratCrusadersHammerTrigger::IsActive()
{
    return !botAI->IsTank(bot) && botAI->IsRanged(bot) && DathrohanNear(botAI, context, bot);
}

bool StratFrontalConeTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    FrontalCone const* cone = nullptr;
    return ConeOver(botAI, context, bot, FRONTAL_CONES, cone) != nullptr;
}

bool StratDivineRetributionTrigger::IsActive()
{
    return RetributionNear(botAI, context, bot) != nullptr;
}
