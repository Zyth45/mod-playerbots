/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rule of the AI budget (no game type, tested in tests/coa_world_ai_budget_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDAIBUDGETRULES_H
#define PLAYERBOTS_COAWORLDAIBUDGETRULES_H

namespace CoaWorldAiBudgetRules
{
// A bot skips its AI this tick only when the map's budget is spent, it did not skip the tick before (no bot waits
// two ticks in a row), and it is not in combat, nor grouped with a real player, nor near one.
inline bool Skip(bool overBudget, bool skippedLast, bool inCombat, bool realGroup, bool realNear)
{
    return overBudget && !skippedLast && !inCombat && !realGroup && !realNear;
}
}  // namespace CoaWorldAiBudgetRules

#endif
