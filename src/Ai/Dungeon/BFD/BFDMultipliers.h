/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BFDMULTIPLIERS_H
#define PLAYERBOTS_BFDMULTIPLIERS_H

#include "Multiplier.h"

class GhamooraMultiplier : public Multiplier
{
public:
    GhamooraMultiplier(PlayerbotAI* ai) : Multiplier(ai, "ghamoo-ra") {}
    float GetValue(Action* action) override;
};

#endif
