/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRSMultipliers.h"

#include "AiObjectContext.h"
#include "BRSTriggers.h"
#include "Playerbots.h"

using namespace BlackrockSpire;
using namespace DungeonBoss;

// Melee walk back in only once the storm is over.
float RendBlackhandMultiplier::GetValue(Action* action)
{
    if (botAI->IsTank(bot) || !MovesIntoDanger(action))
        return 1.0f;

    return RendStorming(botAI, context) ? 0.0f : 1.0f;
}

// Walking back to the boss from the side of the line leads straight across it.
float LineAttackMultiplier::GetValue(Action* action)
{
    if (!MovesIntoDanger(action))
        return 1.0f;

    for (LineAttack const& attack : LINE_ATTACKS)
    {
        Unit* boss = AI_VALUE2(Unit*, "find target", attack.boss);
        Unit* target = boss && boss->IsAlive() ? CastTarget(boss, attack.spell) : nullptr;
        if (target && target != bot)
            return 0.0f;
    }
    return 1.0f;
}
