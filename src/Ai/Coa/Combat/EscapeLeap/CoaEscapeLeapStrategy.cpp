/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEscapeLeapStrategy.h"

#include "Action.h"
#include "CoaCombatGroupTank.h"
#include "CoaEscapeLeapRule.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"

float CoaEscapeLeapMultiplier::GetValue(Action* action)
{
    // Cheap test first: every other action keeps its weight without looking at the group.
    if (!action || !CoaEscapeLeapRule::IsEscapeLeap(action->getName()))
        return 1.0f;

    Map* map = bot->GetMap();

    CoaEscapeLeapRule::Situation s;
    s.combatEngine = combatEngine;
    s.inDungeonOrRaid = map && map->IsDungeon();  // true for raid maps as well
    s.groupHasTank = CoaCombatGroupTank::Of(bot) != nullptr;
    if (map && map->IsRaid())
    {
        InstanceScript* instance = bot->GetInstanceScript();
        s.raidEncounterInProgress = instance && instance->IsEncounterInProgress();
    }

    return CoaEscapeLeapRule::Forbids(s) ? 0.0f : 1.0f;
}

void CoaEscapeLeapStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaEscapeLeapMultiplier(botAI, combatEngine));
}
