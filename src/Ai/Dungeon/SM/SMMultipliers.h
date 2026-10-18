/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMMULTIPLIERS_H
#define PLAYERBOTS_SMMULTIPLIERS_H

#include "Multiplier.h"

class ArcanistDoanMultiplier : public Multiplier
{
public:
    ArcanistDoanMultiplier(PlayerbotAI* ai) : Multiplier(ai, "arcanist doan") {}
    float GetValue(Action* action) override;
};

class HerodMultiplier : public Multiplier
{
public:
    HerodMultiplier(PlayerbotAI* ai) : Multiplier(ai, "herod") {}
    float GetValue(Action* action) override;
};

class WhitemaneMultiplier : public Multiplier
{
public:
    WhitemaneMultiplier(PlayerbotAI* ai) : Multiplier(ai, "high inquisitor whitemane") {}
    float GetValue(Action* action) override;
};

#endif
