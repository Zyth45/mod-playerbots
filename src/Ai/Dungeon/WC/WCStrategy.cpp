/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "WCStrategy.h"

#include "WCMultipliers.h"

void VanillaDungeonWCStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Lady Anacondra, Lord Cobrahn, Lord Pythas, Lord Serpentis, Kresh, Verdan the Everliving, Mutanus the Devourer:
    // their CoA kits are Fierce Blow on the tank, interruptible heals and crowd control (Healing Touch, Sleep,
    // Druid's Slumber, Mass Entanglement, Naralex's Nightmare), ground effects (Raging Storm, Acid Cloud) and
    // point blank spells of 10 yd (Thunderclap, Thunder Clap, Grasping Vines). The class interrupt and dispel
    // actions, "avoid aoe" and "coa boss dodge" cover them. Brightwing's Power Word: Shield is left to the purge.

    // Skum: everyone, the tank too, steps out of the line of his Overrun before he charges.
    triggers.push_back(new TriggerNode("wc skum overrun",
            { NextAction("wc move from overrun", ACTION_RAID + 2) }));
}

void VanillaDungeonWCStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new SkumMultiplier(botAI));
}
