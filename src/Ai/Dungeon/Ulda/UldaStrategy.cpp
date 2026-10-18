/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UldaStrategy.h"

void VanillaDungeonUldaStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Revelosh, the Lost Dwarves, Ironaya, the Obsidian Sentinel, the Ancient Stone Keeper, Galgann Firehammer and
    // Grimlok: their CoA kits are Fierce Blow on the tank, interruptible casts (Chain Lightning, Chain Bolt,
    // Shrink), adds the group kills as they come (Obsidian Shards, Sand Storms, the awakened Earthen) and point
    // blank spells of up to 10 yd (War Stomp, Fire Nova). The class actions and "coa boss dodge" cover them.

    // Archaedas: casters and healers stand outside Ground Tremor.
    triggers.push_back(new TriggerNode("ulda ground tremor",
            { NextAction("ulda move from ground tremor", ACTION_RAID) }));
}
