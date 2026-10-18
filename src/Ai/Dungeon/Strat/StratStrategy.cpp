/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "StratStrategy.h"

void VanillaDungeonStratStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // The other bosses (the Scarlet side's Archivist Galford, Hearthsinger Forresten, Cannon Master Willey and
    // Malor; the Undead side's Baroness Anastari, Nerub'enkan, Maleki, Magistrate Barthilas, Ramstein and Baron
    // Rivendare): their CoA kits are Fierce Blow on the tank, interruptible casts (Pyroblast, Ice Tomb, Enchanting
    // Lullaby, Drain Life, Encasing Webs), dispellable curses, adds, ground effects (Venom Belch, Hour of
    // Judgement) and point blank spells of up to 15 yd (Fire Nova, Crowd Pummel, Ground Smash, Shadow Shock). The
    // class actions, "avoid aoe" and "coa boss dodge" cover them.

    // Grand Crusader Dathrohan: casters and healers stand outside Crusader's Hammer.
    triggers.push_back(new TriggerNode("strat crusader's hammer",
            { NextAction("strat move from crusader's hammer", ACTION_RAID) }));

    // Balnazzar: nobody but the tank stands in front of Carrion Swarm.
    triggers.push_back(new TriggerNode("strat frontal cone",
            { NextAction("strat move from frontal cone", ACTION_RAID + 1) }));

    // Timmy the Cruel: the others step away from the tank just before Divine Retribution releases.
    triggers.push_back(new TriggerNode("strat divine retribution",
            { NextAction("strat move from divine retribution", ACTION_RAID + 1) }));
}
