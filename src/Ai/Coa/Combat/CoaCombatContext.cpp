/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCombatContext.h"

#include "CoaEscapeLeapStrategy.h"
#include "CoaStuckCombat.h"
#include "CoaStuckCombatStrategy.h"
#include "CoaTelegraph.h"

CoaCombatStrategyContext::CoaCombatStrategyContext() : NamedObjectContext<Strategy>(false, true)
{
    creators["coa combat escape leap"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaEscapeLeapStrategy(ai, true); };
    creators["coa combat escape leap nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaEscapeLeapStrategy(ai, false); };
    creators["coa combat stuck combat"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaStuckCombatStrategy(ai, true); };
    creators["coa combat stuck combat nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaStuckCombatStrategy(ai, false); };
    creators["coa combat telegraph"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaTelegraphStrategy(ai); };
}

CoaCombatTriggerContext::CoaCombatTriggerContext() : NamedObjectContext<Trigger>(false, true)
{
    creators["coa combat break stuck combat"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaBreakStuckCombatTrigger(ai); };
    creators["coa combat telegraph"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaTelegraphTrigger(ai); };
}

CoaCombatActionContext::CoaCombatActionContext() : NamedObjectContext<Action>(false, true)
{
    creators["coa combat break stuck combat"] = [](PlayerbotAI* ai) -> Action* { return new CoaBreakStuckCombatAction(ai); };
    creators["coa combat telegraph"] = [](PlayerbotAI* ai) -> Action* { return new CoaTelegraphAction(ai); };
}
