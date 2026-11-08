/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_STUCK_COMBAT_STRATEGY_H
#define PLAYERBOTS_COA_STUCK_COMBAT_STRATEGY_H

#include "Strategy.h"

// "coa combat stuck combat" rides the combat engine, "coa combat stuck combat nc" the non-combat one; every bot has
// both. The non-combat one matters: "drop target" can put a bot on the non-combat engine while it stays flagged in
// combat, and nothing brings it back while there is nothing to attack (CoaStuckCombat.h).
class CoaStuckCombatStrategy : public Strategy
{
public:
    CoaStuckCombatStrategy(PlayerbotAI* botAI, bool combatEngine) : Strategy(botAI), combatEngine(combatEngine) {}

    std::string const getName() override
    {
        return combatEngine ? "coa combat stuck combat" : "coa combat stuck combat nc";
    }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;

private:
    bool combatEngine;
};

#endif
