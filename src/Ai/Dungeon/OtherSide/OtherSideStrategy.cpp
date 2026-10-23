/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OtherSideStrategy.h"
#include "OtherSideMultipliers.h"

void CoaDungeonOtherSideStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Muzah's swirls, glaives, Glaive Strike and Entangling Web; Drak'math's pillars, orbs, Death
    // Missiles and Death Beam; the Embryo's spikes, barrier, poison swirls and web lines.
    triggers.push_back(new TriggerNode("other side in hazard", {
        NextAction("other side leave hazard", ACTION_EMERGENCY + 10) }));

    // Muzah shoots his marked targets until they stand next to him.
    triggers.push_back(new TriggerNode("other side muzah's target", {
        NextAction("other side close in on muzah", ACTION_EMERGENCY + 3) }));

    // Chimeric Embryo: in the egg phase the group keeps running counterclockwise along the wall, and eggs
    // nobody steps on hatch into spiders.
    triggers.push_back(new TriggerNode("other side egg phase", {
        NextAction("other side run around", ACTION_EMERGENCY + 4) }));
    triggers.push_back(new TriggerNode("other side egg to kick", {
        NextAction("other side kick egg", ACTION_EMERGENCY + 5) }));
    triggers.push_back(new TriggerNode("other side spider", {
        NextAction("other side attack spider", ACTION_RAID + 5) }));
}

void CoaDungeonOtherSideStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new OtherSideHazardMultiplier(botAI));
}
