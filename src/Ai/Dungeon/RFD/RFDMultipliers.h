/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFDMULTIPLIERS_H
#define PLAYERBOTS_RFDMULTIPLIERS_H

#include "Multiplier.h"

class GluttonMultiplier : public Multiplier
{
public:
    GluttonMultiplier(PlayerbotAI* ai) : Multiplier(ai, "glutton") {}
    float GetValue(Action* action) override;
};

class MordreshFireEyeMultiplier : public Multiplier
{
public:
    MordreshFireEyeMultiplier(PlayerbotAI* ai) : Multiplier(ai, "mordresh fire eye") {}
    float GetValue(Action* action) override;
};

#endif
