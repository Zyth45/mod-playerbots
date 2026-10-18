/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRSMULTIPLIERS_H
#define PLAYERBOTS_BRSMULTIPLIERS_H

#include "Multiplier.h"

class RendBlackhandMultiplier : public Multiplier
{
public:
    RendBlackhandMultiplier(PlayerbotAI* ai) : Multiplier(ai, "warchief rend blackhand") {}
    float GetValue(Action* action) override;
};

class LineAttackMultiplier : public Multiplier
{
public:
    LineAttackMultiplier(PlayerbotAI* ai) : Multiplier(ai, "brs line attack") {}
    float GetValue(Action* action) override;
};

#endif
