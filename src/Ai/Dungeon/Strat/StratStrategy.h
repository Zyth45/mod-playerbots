/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STRATSTRATEGY_H
#define PLAYERBOTS_STRATSTRATEGY_H

#include "Strategy.h"

class VanillaDungeonStratStrategy : public Strategy
{
public:
    VanillaDungeonStratStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string const getName() override { return "vanilla-strat"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif
