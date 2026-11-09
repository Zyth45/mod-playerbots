/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40MULTIPLIERS_H
#define PLAYERBOTS_AQ40MULTIPLIERS_H

#include "Multiplier.h"

class Aq40CthunMultiplier : public Multiplier
{
public:
    Aq40CthunMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 cthun") {}
    float GetValue(Action* action) override;
};

class Aq40SkeramMultiplier : public Multiplier
{
public:
    Aq40SkeramMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 skeram") {}
    float GetValue(Action* action) override;
};

// CoA Combat (SA1): no interrupt on Sartura from 3 stacks of Frustration (the 4th starts Furious Outbreak).
class Aq40SarturaInterruptMultiplier : public Multiplier
{
public:
    Aq40SarturaInterruptMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "aq40 sartura interrupt") {}
    float GetValue(Action* action) override;
};

#endif
