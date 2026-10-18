/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ScholoStrategy.h"

void VanillaDungeonScholoStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Kirtonos, Jandice Barov, Rattlegore, Marduk Blackpool, Vectus, Ras Frostwhisper, Instructor Malicia, Doctor
    // Theolen Krastinov, Lady Illucia Barov and Darkmaster Gandling: their CoA kits are Fierce Blow on the tank,
    // interruptible casts and crowd control (Shadow Bolt Volley, Frostbolt Volley, Dominate Mind, Krastinov's
    // Ghoulish Potion, Banish, Incinerate), adds (Rise!, Illusions), ground effects (Flamestrike) and point blank
    // spells of 10 yd (War Stomp, Chill Nova). The class actions, "avoid aoe" and "coa boss dodge" cover them.

    // Lorekeeper Polkelt, The Ravenian, Lord Alexei Barov: nobody stands in a cone aimed at someone else.
    triggers.push_back(new TriggerNode("scholo frontal cone",
            { NextAction("scholo move from frontal cone", ACTION_RAID + 1) }));
}
