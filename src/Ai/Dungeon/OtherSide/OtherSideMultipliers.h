/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_OTHERSIDEMULTIPLIERS_H
#define PLAYERBOTS_OTHERSIDEMULTIPLIERS_H

#include "Multiplier.h"

class OtherSideHazardMultiplier : public Multiplier
{
public:
    OtherSideHazardMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "other side hazard") {}
    float GetValue(Action* action) override;
};

#endif
