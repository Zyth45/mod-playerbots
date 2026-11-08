/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCombatContext.h"

#include "CoaEscapeLeapStrategy.h"

CoaCombatStrategyContext::CoaCombatStrategyContext() : NamedObjectContext<Strategy>(false, true)
{
    creators["coa combat escape leap"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaEscapeLeapStrategy(ai, true); };
    creators["coa combat escape leap nc"] = [](PlayerbotAI* ai) -> Strategy* { return new CoaEscapeLeapStrategy(ai, false); };
}
