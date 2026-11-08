/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaStuckCombatStrategy.h"

#include "CoaStuckCombatRule.h"
#include "Trigger.h"

void CoaStuckCombatStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("coa combat break stuck combat",
                                       {NextAction("coa combat break stuck combat", CoaStuckCombatRule::RELEVANCE)}));
}
