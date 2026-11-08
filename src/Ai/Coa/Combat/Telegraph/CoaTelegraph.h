/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_TELEGRAPH_H
#define PLAYERBOTS_COA_TELEGRAPH_H

#include "MovementActions.h"
#include "Strategy.h"
#include "Trigger.h"

class DynamicObject;

// Leave a telegraphed ground marker (CoaTelegraphRule.h). Combat engine, every bot, when
// AiPlayerbot.CoaCombat.Telegraph.Enable is 1 (0 by default).
namespace CoaTelegraph
{
    // The marker the bot stands in and must leave now, or nullptr. `safeDistance` gets the distance to reach.
    DynamicObject* MarkerToLeave(Player* bot, float& safeDistance);
}

class CoaTelegraphTrigger : public Trigger
{
public:
    CoaTelegraphTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa combat telegraph") {}
    bool IsActive() override;
};

class CoaTelegraphAction : public MovementAction
{
public:
    CoaTelegraphAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa combat telegraph") {}
    bool Execute(Event event) override;
};

class CoaTelegraphStrategy : public Strategy
{
public:
    CoaTelegraphStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa combat telegraph"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif
