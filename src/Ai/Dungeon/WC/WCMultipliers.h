/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCMULTIPLIERS_H
#define PLAYERBOTS_WCMULTIPLIERS_H

#include "Multiplier.h"

class SkumMultiplier : public Multiplier
{
public:
    SkumMultiplier(PlayerbotAI* ai) : Multiplier(ai, "skum") {}
    float GetValue(Action* action) override;
};

#endif
