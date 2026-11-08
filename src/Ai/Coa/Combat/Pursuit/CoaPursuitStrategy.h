/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_PURSUIT_STRATEGY_H
#define PLAYERBOTS_COA_PURSUIT_STRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

// Zeroes a non-tank bot's chase toward a fleeing mob when it would end beyond the leash from the tank
// (CoaPursuitRule.h). Combat engine, in a dungeon or raid group, when AiPlayerbot.CoaCombat.PursuitStop.Enable is 1.
class CoaPursuitMultiplier : public Multiplier
{
public:
    CoaPursuitMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa combat pursuit stop") {}
    float GetValue(Action* action) override;
};

class CoaPursuitStrategy : public Strategy
{
public:
    CoaPursuitStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa combat pursuit stop"; }
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
