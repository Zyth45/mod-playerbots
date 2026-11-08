/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEscapeLeapStrategy.h"

#include "Action.h"
#include "CoaEscapeLeapRule.h"
#include "CoaGroupTank.h"
#include "CoaGardesGate.h"
#include "CoaGardesRunHooks.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"

float CoaEscapeLeapMultiplier::GetValue(Action* action)
{
    // Cheap test first: every other action keeps its weight without looking at the group.
    if (!action || !CoaEscapeLeapRule::IsEscapeLeap(action->getName()))
        return 1.0f;

    // Silent in a Dungeon Clear run while Dungeon Clear still runs its own copy ("escape leap ban").
    if (!CoaGroupTank::ModuleGuardActs(bot, CoaGardesGate::ESCAPE_LEAP_BAN))
        return 1.0f;

    Map* map = bot->GetMap();

    CoaEscapeLeapRule::Situation s;
    s.combatEngine = combatEngine;
    s.inDungeonOrRaid = map && map->IsDungeon();  // true for raid maps as well
    // The group's reference tank (the run leader, the player Main Tank, the main tank flag, the first tank), on the
    // bot's map; it must also be alive.
    Player* tank = CoaGroupTank::Reference(bot);
    s.groupHasTank = tank && tank->IsInWorld() && tank->IsAlive();
    if (map && map->IsRaid())
    {
        InstanceScript* instance = bot->GetInstanceScript();
        // The instance script's encounter state, or the run's own stand-down (Dungeon Clear's hook, with its fallback
        // on a roster boss holding a member).
        s.raidEncounterInProgress = (instance && instance->IsEncounterInProgress()) ||
                                    (CoaGroupTank::InDungeonClearRun(bot) && CoaGardesRunHooks::IsRaidStandDown(bot));
    }

    return CoaEscapeLeapRule::Forbids(s) ? 0.0f : 1.0f;
}

void CoaEscapeLeapStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new CoaEscapeLeapMultiplier(botAI, combatEngine));
}
