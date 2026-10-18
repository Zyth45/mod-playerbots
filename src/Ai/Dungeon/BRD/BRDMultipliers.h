/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRDMULTIPLIERS_H
#define PLAYERBOTS_BRDMULTIPLIERS_H

#include "Multiplier.h"

class AmbassadorFlamelashMultiplier : public Multiplier
{
public:
    AmbassadorFlamelashMultiplier(PlayerbotAI* ai) : Multiplier(ai, "ambassador flamelash") {}
    float GetValue(Action* action) override;
};

class ThaurissanMultiplier : public Multiplier
{
public:
    ThaurissanMultiplier(PlayerbotAI* ai) : Multiplier(ai, "emperor dagran thaurissan") {}
    float GetValue(Action* action) override;
};

#endif
