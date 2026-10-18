/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFKStrategy.h"

void VanillaDungeonRFKStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Roogug, Aggem Thorncurse, Overlord Ramtusk, Agathelos the Raging, Blind Hunter, Earthcaller Halmgar: their CoA
    // kits are Fierce Blow on the tank, interruptible casts (Healing Stream, Lightning Bolt), adds that the dungeon
    // priorities kill first (Greater Earthgrab Totem), Earthquake on the ground and point blank spells of 10 yd
    // (Thunder Clap, Rampage). The class actions, "avoid aoe" and "coa boss dodge" cover them. Jargba's Dominate
    // Mind is interrupted first; the charmed player is left alone.

    // Charlga Razorflank: Lightning Nova (2102774, a 6 s cast, 40 yd) is interruptible, she is not immune to
    // interrupts and it is on the dungeon interrupt priority list, so the group kicks it instead of running out.

    // Death Speaker Jargba: nobody but the tank stands in front of Shadow Bolt Volley. "coa boss dodge" only acts
    // on cones of up to 15 yd.
    triggers.push_back(new TriggerNode("rfk frontal cone",
            { NextAction("rfk move from frontal cone", ACTION_RAID + 1) }));
}
