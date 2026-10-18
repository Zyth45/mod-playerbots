/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRDSTRATEGY_H
#define PLAYERBOTS_BRDSTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class VanillaDungeonBRDStrategy : public Strategy
{
public:
    VanillaDungeonBRDStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string const getName() override { return "vanilla-brd"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
