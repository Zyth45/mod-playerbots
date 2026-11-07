/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFCStrategy.h"
#include "RFCMultipliers.h"

void VanillaDungeonRFCStrategy::InitTriggers(std::vector<TriggerNode*> &triggers)
{
    // Oggleflint
    // His escort is a Ragefire Shaman and a Ragefire Trogg. The Shaman carries Healing Wave on a
    // three second cast, so it is the one worth killing first.
    triggers.push_back(new TriggerNode("oggleflint adds",
             { NextAction("attack oggleflint add", ACTION_RAID + 1) }));

    // Taragaman the Hungerer
    // Fire Nova reaches ten yards from the boss. Only the ranged are moved; melee have to be there.
    //
    triggers.push_back(new TriggerNode("taragaman fire nova",
            { NextAction("avoid fire nova", ACTION_MOVE + 5) }));

    // Uppercut's knockback is instant, so it is answered by where the tank stands: Taragaman stays on his spawn
    // point in the middle of the rock cross, the tank on the longest arm next to him, and an Uppercut slides it along
    // the rock. After a throw the tank walks back and brings him along.
    triggers.push_back(new TriggerNode("taragaman tank spot",
            { NextAction("taragaman hold center", ACTION_RAID + 3) }));

    // Massive Fire Nova covers the whole room; the rock pillar he raises shortly before is the
    // only cover. Everyone but the tank goes behind it as soon as it appears, the tank once the
    // cast has started.
    triggers.push_back(new TriggerNode("taragaman massive fire nova",
            { NextAction("hide behind earth pillar", ACTION_EMERGENCY + 6) }));

    // The pillar breaks out of the ground five seconds after it appears, under a player, and kills
    // whoever still stands in its swirling ground warning. Everyone leaves it, the tank included.
    triggers.push_back(new TriggerNode("taragaman earth break",
            { NextAction("avoid earth break", ACTION_EMERGENCY + 7) }));

    // Jergosh the Invoker
    triggers.push_back(new TriggerNode("jergosh adds",
            { NextAction("attack jergosh add", ACTION_RAID + 1) }));

    // Bazzalan
    // The reverse of the other two rooms: he deals far more single target damage than his Cultists,
    // so he goes down first and they are cleaned up afterwards.
    triggers.push_back(new TriggerNode("bazzalan priority",
            { NextAction("attack bazzalan", ACTION_RAID + 1) }));
}

void VanillaDungeonRFCStrategy::InitMultipliers(std::vector<Multiplier*> &multipliers)
{
    multipliers.push_back(new OggleflintMultiplier(botAI));
    multipliers.push_back(new TaragamanTheHungererMultiplier(botAI));
    multipliers.push_back(new JergoshTheInvokerMultiplier(botAI));
    multipliers.push_back(new BazzalanMultiplier(botAI));
}
