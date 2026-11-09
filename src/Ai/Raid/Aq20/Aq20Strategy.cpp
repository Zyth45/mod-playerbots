/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq20Strategy.h"
#include "Strategy.h"

void RaidAq20Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode("aq20 move to crystal",
            { NextAction("aq20 use crystal", ACTION_RAID) }));

    // CoA Combat (lot 7, docs/AQ-ETAT-3.0.md): Buru's Devastate (BU2), Ossirian's obelisks (OS1).
    triggers.push_back(
        new TriggerNode("aq20 buru devastate",
            { NextAction("aq20 buru avoid devastate", ACTION_EMERGENCY + 3) }));
    triggers.push_back(
        new TriggerNode("aq20 ossirian shelter",
            { NextAction("aq20 ossirian shelter", ACTION_RAID + 2) }));

}
