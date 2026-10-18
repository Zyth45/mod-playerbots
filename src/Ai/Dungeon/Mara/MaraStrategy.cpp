/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MaraStrategy.h"

#include "MaraMultipliers.h"

void VanillaDungeonMaraStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Noxxion, Razorlash, Celebras the Cursed, Meshlok the Harvester, Rotgrip: their CoA kits are Fierce Blow on
    // the tank, interruptible casts (Wrath, Twisted Tranquility), adds and spells that reach the whole room (Toxic
    // Volley, 45 yd). The class actions and "coa boss dodge" cover them, as they do Gizlock's Bomb and Theradras's
    // Repulsive Gaze. Landslide's Gigantic Boulder at a random player has no cast bar to gather on.

    // Landslide: everyone comes to share the Hurl Boulder cast at the tank.
    triggers.push_back(new TriggerNode("mara hurl boulder",
            { NextAction("mara share boulder", ACTION_RAID + 2) }));

    // Lord Vyletongue (Focused Burst) and Tinkerer Gizlock (Goblin Dragon Gun): nobody but the target stands in
    // the cone.
    triggers.push_back(new TriggerNode("mara frontal cone",
            { NextAction("mara move from frontal cone", ACTION_RAID + 1) }));

    // Princess Theradras: the damage dealers and healers stay out of Dust Field while it lasts; it is an aura, so
    // "coa boss dodge" does not see it.
    triggers.push_back(new TriggerNode("mara dust field",
            { NextAction("mara move from dust field", ACTION_RAID + 1) }));
}

void VanillaDungeonMaraStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new LandslideMultiplier(botAI));
    multipliers.push_back(new TheradrasMultiplier(botAI));
}
