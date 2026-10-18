/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BRSStrategy.h"

#include "BRSMultipliers.h"

void VanillaDungeonBRSStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Lower Spire (Highlord Omokk, Shadow Hunter Vosh'gajin, War Master Voone, Mother Smolderweb, Urok Doomhowl,
    // Quartermaster Zigris, Halycon, Gizrul, Overlord Wyrmthalak and the rares) and the rest of the Upper Spire
    // (Pyroguard Emberseer, Solakar Flamewreath, Jed Runewatcher, Goraluk Anvilcrack): their CoA kits are Fierce
    // Blow on the tank, interruptible casts (Pyroblast, Polymorph, Hex, Arcane Bolt), ground effects (Flamestrike)
    // and point blank spells of up to 10 yd. The class actions, "avoid aoe" and "coa boss dodge" cover them, as
    // they do The Beast's Flame Breath and Blast Wave and Emberseer's Fire Nova.

    // General Drakkisath (Heatwave) and The Beast (Overrun): everyone but the player aimed at steps out of the
    // line before it is swept.
    triggers.push_back(new TriggerNode("brs line attack",
            { NextAction("brs move from line attack", ACTION_RAID + 2) }));

    // Warchief Rend Blackhand: the damage dealers and healers stay out of Bladestorm; it is an aura once cast, so
    // "coa boss dodge" does not see it.
    triggers.push_back(new TriggerNode("brs rend bladestorm",
            { NextAction("brs move from bladestorm", ACTION_RAID + 2) }));

    // Gyth: nobody but the tank stands in front of his breaths. "coa boss dodge" acts on cones of up to 15 yd
    // only, and only on cast bars.
    triggers.push_back(new TriggerNode("brs frontal cone",
            { NextAction("brs move from frontal cone", ACTION_RAID + 1) }));
}

void VanillaDungeonBRSStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new LineAttackMultiplier(botAI));
    multipliers.push_back(new RendBlackhandMultiplier(botAI));
}
