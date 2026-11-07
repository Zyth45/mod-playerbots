/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UKStrategy.h"
#include "UKMultipliers.h"

void WotlkDungeonUKStrategy::InitTriggers(std::vector<TriggerNode*> &triggers)
{
    // Prince Keleseth
    triggers.push_back(new TriggerNode("keleseth frost tomb",
             { NextAction("attack frost tomb", ACTION_RAID + 1) }));

    // Skarvald the Constructor & Dalronn the Controller
    triggers.push_back(new TriggerNode("dalronn priority",
             { NextAction("attack dalronn", ACTION_RAID + 1) }));

    // Ingvar the Plunderer

    // His roars need line of sight, which the pillar ring next to his spawn point blocks: the tank holds him on his
    // spawn point, the ranged and healers stand one step from cover, and when a roar begins everyone, the tank and
    // melee too, ducks behind the pillars and comes back afterwards.
    triggers.push_back(new TriggerNode("ingvar roar",
            { NextAction("ingvar hide from roar", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("ingvar tank spot",
            { NextAction("ingvar hold spot", ACTION_RAID + 3) }));
    triggers.push_back(new TriggerNode("ingvar cover ready",
            { NextAction("ingvar cover ready", ACTION_MOVE + 4) }));
    triggers.push_back(new TriggerNode("ingvar smash tank",
            { NextAction("ingvar dodge smash", ACTION_MOVE + 5) }));
    triggers.push_back(new TriggerNode("ingvar smash tank return",
            { NextAction("ingvar smash return", ACTION_MOVE + 5) }));
    // Buggy... if not behind target, ai can get stuck running towards and away from target.
    // I think for ranged chars, a custom action should be added that doesn't attempt to run into melee.
    // This is a bandaid for now, needs to be improved.
    triggers.push_back(new TriggerNode("not behind ingvar",
            { NextAction("set behind", ACTION_MOVE + 1) }));

}

void WotlkDungeonUKStrategy::InitMultipliers(std::vector<Multiplier*> &multipliers)
{
    multipliers.push_back(new PrinceKelesethMultiplier(botAI));
    multipliers.push_back(new SkarvaldAndDalronnMultiplier(botAI));
    multipliers.push_back(new IngvarThePlundererMultiplier(botAI));
}
