/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SMStrategy.h"
#include "SMMultipliers.h"

void VanillaDungeonSMStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Arcanist Doan
    triggers.push_back(new TriggerNode("arcanist doan boss casting detonation", {
        NextAction("arcanist doan move out of detonation", ACTION_EMERGENCY + 10) }));
}

void VanillaDungeonSMStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new ArcanistDoanDetonationMultiplier(botAI));
}
