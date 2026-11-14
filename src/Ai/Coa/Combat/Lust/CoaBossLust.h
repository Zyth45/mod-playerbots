/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_BOSS_LUST_H
#define PLAYERBOTS_COA_BOSS_LUST_H

#include "CoaBossLustRule.h"
#include "GenericTriggers.h"
#include "Multiplier.h"
#include "Strategy.h"

class PlayerbotAI;

namespace CoaBossLust
{
    // The rule's verdict for this bot now (CoaBossLustRule::Decide).
    CoaBossLustRule::Verdict Verdict(PlayerbotAI* botAI);
}

// A grouped shaman casts Bloodlust or Heroism on a boss: at the pull, or under AiPlayerbot.CoaCombat.LustBossHealthPct.
// The stock boost trigger only fires when the fight looks lopsided, which a 5-man boss alone rarely does.
class CoaBossLustTrigger : public BuffTrigger
{
public:
    CoaBossLustTrigger(PlayerbotAI* botAI, std::string const spell) : BuffTrigger(botAI, spell, 1) {}
    bool IsActive() override;
};

// Keeps Bloodlust and Heroism off trash: zero while the rule forbids them, whatever trigger asks.
class CoaBossLustMultiplier : public Multiplier
{
public:
    CoaBossLustMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa combat boss lust") {}
    float GetValue(Action* action) override;
};

class CoaBossLustStrategy : public Strategy
{
public:
    CoaBossLustStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "coa combat boss lust"; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
    void InitMultipliers(std::vector<Multiplier*>& multipliers) override;
};

#endif
