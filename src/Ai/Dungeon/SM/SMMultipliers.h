/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMMULTIPLIERS_H
#define PLAYERBOTS_SMMULTIPLIERS_H

#include "Multiplier.h"

class ArcanistDoanDetonationMultiplier : public Multiplier
{
public:
    ArcanistDoanDetonationMultiplier(PlayerbotAI* botAI)
        : Multiplier(botAI, "arcanist doan detonation") {}
    float GetValue(Action* action) override;
};

#endif
