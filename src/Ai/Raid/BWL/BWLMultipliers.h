/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BWLMULTIPLIERS_H
#define PLAYERBOTS_BWLMULTIPLIERS_H

#include "Multiplier.h"

class RazorgoreTankMultiplier : public Multiplier
{
public:
    RazorgoreTankMultiplier(
        PlayerbotAI* botAI) : Multiplier(botAI, "razorgore tank multiplier") {}
    float GetValue(Action* action) override;
};

// CoA Combat B6: a bot that holds Razorgore's possession (the Orb of Domination's mind control, 19832, a channel on the
// bot's own body) does nothing else. Any cast, swing, step, potion or target drop ends the channel: the boss is loose
// in phase 1, where killing him wipes the raid, and the bot is locked out of the orb for 60 s. A human raider holding
// the orb cannot act either. Who goes to the orb (Dungeon Clear's event) is not this multiplier's business.
class RazorgorePossessionMultiplier : public Multiplier
{
public:
    RazorgorePossessionMultiplier(
        PlayerbotAI* botAI) : Multiplier(botAI, "razorgore possession multiplier") {}
    float GetValue(Action* action) override;
};

// CoA Combat (F1 / E1): a tank that may not take Firemaw or Ebonroc yet does not taunt it back from the tank that
// just took it over.
class BwlDrakeTauntHoldMultiplier : public Multiplier
{
public:
    BwlDrakeTauntHoldMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "bwl drake taunt hold multiplier") {}
    float GetValue(Action* action) override;
};

// CoA Combat (N1): no area spell or cleave lands near a Dominion clone; every hit on it lands on its owner.
class BwlDominionCloneAoeMultiplier : public Multiplier
{
public:
    BwlDominionCloneAoeMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "bwl dominion clone aoe multiplier") {}
    float GetValue(Action* action) override;
};

class VaelastraszTankMultiplier : public Multiplier
{
public:
    VaelastraszTankMultiplier(
        PlayerbotAI* botAI) : Multiplier(botAI, "vaelastrasz tank multiplier") {}
    float GetValue(Action* action) override;
};

class VaelastraszBurningAdrenalineMultiplier : public Multiplier
{
public:
    VaelastraszBurningAdrenalineMultiplier(
        PlayerbotAI* botAI) : Multiplier(botAI, "vaelastrasz burning adrenaline multiplier") {}
    float GetValue(Action* action) override;
};

#endif
