/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCSTRATEGY_H
#define PLAYERBOTS_WCSTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class VanillaDungeonWCStrategy : public Strategy
{
public:
    VanillaDungeonWCStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string const getName() override { return "vanilla-wc"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
