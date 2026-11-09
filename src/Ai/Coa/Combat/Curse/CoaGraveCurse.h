/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_GRAVE_CURSE_H
#define PLAYERBOTS_COA_GRAVE_CURSE_H

#include "Action.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "Strategy.h"
#include "Trigger.h"

class Creature;

// Call of the Grave (CoaGraveCurseRule.h): every bot that can remove a curse removes it first, and the melee damage
// dealers keep out of the casters' 5 yd. Combat engine, every bot, when AiPlayerbot.CoaCombat.CallOfTheGrave.Enable
// is 1 (0 by default).
namespace CoaGraveCurse
{
    // The spell of this bot that removes a curse from an ally (by its effect, so CoA classes are covered), or 0.
    uint32 CurseRemovalSpell(Player* bot);
    // The group member within 40 yd who carries Call of the Grave, or nullptr.
    Player* CursedMember(Player* bot);
    // The living Spectre or Phantasm within `range` of `from`, or nullptr.
    Creature* GraveCasterNear(Unit* from, float range);
}

class CoaGraveCurseDispelTrigger : public Trigger
{
public:
    CoaGraveCurseDispelTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa combat grave curse dispel") {}
    bool IsActive() override;
};

class CoaGraveCurseDispelAction : public Action
{
public:
    CoaGraveCurseDispelAction(PlayerbotAI* botAI) : Action(botAI, "coa combat grave curse dispel") {}
    bool Execute(Event event) override;
};

class CoaGraveCurseLeaveTrigger : public Trigger
{
public:
    CoaGraveCurseLeaveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa combat grave curse leave") {}
    bool IsActive() override;
};

class CoaGraveCurseLeaveAction : public MovementAction
{
public:
    CoaGraveCurseLeaveAction(PlayerbotAI* botAI) : MovementAction(botAI, "coa combat grave curse leave") {}
    bool Execute(Event event) override;
};

// The melee damage dealers do not walk back within reach of a caster to strike.
class CoaGraveCurseMeleeMultiplier : public Multiplier
{
public:
    CoaGraveCurseMeleeMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa combat grave curse melee") {}
    float GetValue(Action* action) override;
};

class CoaGraveCurseStrategy : public Strategy
{
public:
    CoaGraveCurseStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa combat call of the grave"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
