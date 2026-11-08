/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AiObjectContext.h"
#include "CoaCombatContext.h"
#include "CoaGardesContext.h"
#include "DungeonStrategyContext.h"
#include "RaidStrategyContext.h"
#include "CoaGenericContext.h"
#include "StrategyContext.h"

void AiObjectContext::BuildSharedStrategyContexts(SharedNamedObjectContextList<Strategy>& strategyContexts)
{
    strategyContexts.Add(new StrategyContext());
    strategyContexts.Add(new MovementStrategyContext());
    strategyContexts.Add(new AssistStrategyContext());
    strategyContexts.Add(new QuestStrategyContext());
    strategyContexts.Add(new DungeonStrategyContext());
    strategyContexts.Add(new RaidStrategyContext());
    strategyContexts.Add(new CoaGenericStrategyContext());
    strategyContexts.Add(new CoaGardesStrategyContext());
    // CoA Combat
    strategyContexts.Add(new CoaCombatStrategyContext());
}
