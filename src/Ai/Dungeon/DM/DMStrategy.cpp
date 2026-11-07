/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DMStrategy.h"

#include "DMMultipliers.h"

void VanillaDungeonDMStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Hydrospawn: everyone leaves a Massive Geyser, and the player it is cast on leaves the group so that it rises away
    // from everyone; the ranged and healers stand apart against Geyser; the damage dealers clear the Hydrolings once
    // three are up. Riptide (10 yd around him), Submersion and Fierce Blow are left to the healers.
    triggers.push_back(new TriggerNode("dm massive geyser",
            { NextAction("dm leave massive geyser", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("dm geyser spread",
            { NextAction("dm geyser spread", ACTION_MOVE + 5) }));
    triggers.push_back(new TriggerNode("dm hydrolings",
            { NextAction("dm attack hydroling", ACTION_RAID + 1) }));
}

void VanillaDungeonDMStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new HydrospawnMultiplier(botAI));
}
