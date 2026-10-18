/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFKSTRATEGY_H
#define PLAYERBOTS_RFKSTRATEGY_H

#include "Multiplier.h"
#include "Strategy.h"

class VanillaDungeonRFKStrategy : public Strategy
{
public:
    VanillaDungeonRFKStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string const getName() override { return "vanilla-rfk"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif
