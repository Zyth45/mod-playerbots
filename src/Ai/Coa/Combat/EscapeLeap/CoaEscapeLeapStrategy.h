/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_ESCAPE_LEAP_STRATEGY_H
#define PLAYERBOTS_COA_ESCAPE_LEAP_STRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

// Zeroes Blink and Disengage in a dungeon or raid group with a tank (CoaEscapeLeapRule.h).
class CoaEscapeLeapMultiplier : public Multiplier
{
public:
    CoaEscapeLeapMultiplier(PlayerbotAI* botAI, bool combatEngine)
        : Multiplier(botAI, "coa combat escape leap"), combatEngine(combatEngine)
    {
    }

    float GetValue(Action* action) override;

private:
    bool combatEngine;
};

// "coa combat escape leap" rides the combat engine, "coa combat escape leap nc" the non-combat one; every bot has both.
class CoaEscapeLeapStrategy : public Strategy
{
public:
    CoaEscapeLeapStrategy(PlayerbotAI* botAI, bool combatEngine) : Strategy(botAI), combatEngine(combatEngine) {}

    std::string const getName() override
    {
        return combatEngine ? "coa combat escape leap" : "coa combat escape leap nc";
    }

    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;

private:
    bool combatEngine;
};

#endif
