/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCombatContext.h"

#include "CoaEscapeLeapStrategy.h"
#include "CoaStuckCombat.h"
#include "CoaStuckCombatStrategy.h"
#include "CoaPursuitStrategy.h"
#include "CoaTelegraph.h"
#include "Curse/CoaGraveCurse.h"

CoaCombatStrategyContext::CoaCombatStrategyContext() : NamedObjectContext<Strategy>(false, true)
{
    creators["coa combat escape leap"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaEscapeLeapStrategy(ai, true); };
    creators["coa combat escape leap nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaEscapeLeapStrategy(ai, false); };
    creators["coa combat stuck combat"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaStuckCombatStrategy(ai, true); };
    creators["coa combat stuck combat nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaStuckCombatStrategy(ai, false); };
    creators["coa combat telegraph"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaTelegraphStrategy(ai); };
    creators["coa combat pursuit stop"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaPursuitStrategy(ai); };
    creators["coa combat call of the grave"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaGraveCurseStrategy(ai); };
}

CoaCombatTriggerContext::CoaCombatTriggerContext() : NamedObjectContext<Trigger>(false, true)
{
    creators["coa combat break stuck combat"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaBreakStuckCombatTrigger(ai); };
    creators["coa combat telegraph"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaTelegraphTrigger(ai); };
    creators["coa combat grave curse dispel"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaGraveCurseDispelTrigger(ai); };
    creators["coa combat grave curse leave"] = [](PlayerbotAI* ai) -> Trigger* { return new CoaGraveCurseLeaveTrigger(ai); };
}

CoaCombatActionContext::CoaCombatActionContext() : NamedObjectContext<Action>(false, true)
{
    creators["coa combat break stuck combat"] = [](PlayerbotAI* ai) -> Action* { return new CoaBreakStuckCombatAction(ai); };
    creators["coa combat telegraph"] = [](PlayerbotAI* ai) -> Action* { return new CoaTelegraphAction(ai); };
    creators["coa combat grave curse dispel"] = [](PlayerbotAI* ai) -> Action* { return new CoaGraveCurseDispelAction(ai); };
    creators["coa combat grave curse leave"] = [](PlayerbotAI* ai) -> Action* { return new CoaGraveCurseLeaveAction(ai); };
}
