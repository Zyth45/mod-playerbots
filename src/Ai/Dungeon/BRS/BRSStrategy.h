/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRSSTRATEGY_H
#define PLAYERBOTS_BRSSTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class VanillaDungeonBRSStrategy : public Strategy
{
public:
    VanillaDungeonBRSStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string const getName() override { return "vanilla-brs"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
