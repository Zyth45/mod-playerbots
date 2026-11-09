/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_TELEGRAPH_H
#define PLAYERBOTS_COA_TELEGRAPH_H

#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

class DynamicObject;

// Leave a telegraphed ground marker (CoaTelegraphRule.h). Combat engine, every bot, when
// AiPlayerbot.CoaCombat.Telegraph.Enable is 1 (0 by default).
namespace CoaTelegraph
{
    // The marker the bot stands in and must leave now, or nullptr. `safeDistance` gets the distance to reach.
    DynamicObject* MarkerToLeave(Player* bot, float& safeDistance);
    // A live marker `target` cast and stands within (Targorr leaps on his own spot, SmartAI target self), or nullptr.
    DynamicObject* MarkerUnderCaster(Unit* target, float& safeDistance);
    // A live marker cast by the bot's target or one of its attackers whose hold range (CoaTelegraphRule::HoldApproach)
    // covers the bot, or nullptr.
    DynamicObject* MarkerToHoldFor(Player* bot, Unit* target, GuidVector const& attackers);
}

// While such a marker lies under the bot's target, no melee approach: melee reach of the boss ends inside the 4 yd
// slam, so a bot that stepped out walked straight back in (bench a20, Targorr). Bench a39: no approach move at all
// (reach, attack, follow, regroup, tank assist, heal reposition) while a live marker is near the bot (MarkerToHoldFor).
class CoaTelegraphMeleeMultiplier : public Multiplier
{
public:
    CoaTelegraphMeleeMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa combat telegraph melee") {}
    float GetValue(Action* action) override;
};

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
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
