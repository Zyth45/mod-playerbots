/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFDStrategy.h"

#include "RFDMultipliers.h"

void VanillaDungeonRFDStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Tuten'kash, Plaguemaw the Rotting, Ragglesnout: their CoA kits are Fierce Blow on the tank, interruptible
    // casts and crowd control (Drain Life, Heal, Dominate Mind) and a 10 yd point blank silence (Putrid Stench).
    // The class actions and "coa boss dodge" cover them, as they do Amnennar's Frost Nova.

    // Glutton: everyone, the tank too, leaves Expel Bloat while it is cast. "coa boss dodge" moves only the others.
    triggers.push_back(new TriggerNode("rfd glutton expel bloat",
            { NextAction("rfd move from expel bloat", ACTION_RAID + 2) }));

    // Mordresh Fire Eye: everyone leaves Bone Storm; it is an aura, not a cast, so "coa boss dodge" does not see it.
    triggers.push_back(new TriggerNode("rfd mordresh bone storm",
            { NextAction("rfd move from bone storm", ACTION_RAID + 2) }));

    // Amnennar the Coldbringer: the marked player cannot dodge Steal Warmth or Ice Spikes, so the others step away
    // from them.
    triggers.push_back(new TriggerNode("rfd amnennar mark",
            { NextAction("rfd move from marked player", ACTION_RAID + 1) }));
}

void VanillaDungeonRFDStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new GluttonMultiplier(botAI));
    multipliers.push_back(new MordreshFireEyeMultiplier(botAI));
}
