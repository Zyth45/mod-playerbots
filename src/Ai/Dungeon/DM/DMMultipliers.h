/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DMMULTIPLIERS_H
#define PLAYERBOTS_DMMULTIPLIERS_H

#include "Multiplier.h"

class HydrospawnMultiplier : public Multiplier
{
public:
    HydrospawnMultiplier(PlayerbotAI* ai) : Multiplier(ai, "hydrospawn") {}
    float GetValue(Action* action) override;
};

#endif
