/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRSTriggers.h"

#include "AiObjectContext.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace BlackrockSpire
{
    Unit* LineOver(PlayerbotAI* botAI, AiObjectContext* context, Player* bot, float& angle, float& halfWidth)
    {
        for (LineAttack const& attack : LINE_ATTACKS)
        {
            Unit* boss = AI_VALUE2(Unit*, "find target", attack.boss);
            if (!boss || !boss->IsAlive())
                continue;

            Unit* target = CastTarget(boss, attack.spell);
            if (!target || target == bot)
                continue;

            angle = boss->GetAngle(target);
            halfWidth = attack.radius + bot->GetBoundaryRadius() + LINE_MARGIN +
                        (attack.fromBossBody ? boss->GetBoundaryRadius() : 0.0f);
            float const length = attack.length > 0.0f ? attack.length : boss->GetExactDist2d(target);
            if (InPath(boss, angle, length, halfWidth, bot))
                return boss;
        }
        return nullptr;
    }

    Unit* RendStorming(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* rend = AI_VALUE2(Unit*, "find target", "warchief rend blackhand");
        return rend && rend->IsAlive() && HasAuraNamed(rend, "Bladestorm") ? rend : nullptr;
    }
}

using namespace BlackrockSpire;

bool BrsLineAttackTrigger::IsActive()
{
    float angle = 0.0f;
    float halfWidth = 0.0f;
    return LineOver(botAI, context, bot, angle, halfWidth) != nullptr;
}

bool BrsRendBladestormTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    Unit* rend = RendStorming(botAI, context);
    return rend && Within(bot, rend, BLADESTORM_RADIUS);
}

bool BrsFrontalConeTrigger::IsActive()
{
    if (botAI->IsTank(bot))
        return false;

    FrontalCone const* cone = nullptr;
    return ConeOver(botAI, context, bot, FRONTAL_CONES, cone) != nullptr;
}
