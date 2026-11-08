/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_COMBAT_CONTEXT_H
#define PLAYERBOTS_COA_COMBAT_CONTEXT_H

#include "NamedObjectContext.h"
#include "Strategy.h"

// The strategies of the CoA Combat guards (src/Ai/Coa/Combat), one entry per guard.
class CoaCombatStrategyContext : public NamedObjectContext<Strategy>
{
public:
    CoaCombatStrategyContext();
};

#endif
