/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MCMULTIPLIERS_H
#define PLAYERBOTS_MCMULTIPLIERS_H

#include "Multiplier.h"

class GarrDisableDpsAoeMultiplier : public Multiplier
{
public:
    GarrDisableDpsAoeMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "garr disable dps aoe multiplier") {}
    float GetValue(Action* action) override;
};

class BaronGeddonAbilityMultiplier : public Multiplier
{
public:
    BaronGeddonAbilityMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "baron geddon ability multiplier") {}
    float GetValue(Action* action) override;
};

class GolemaggMultiplier : public Multiplier
{
public:
    GolemaggMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "golemagg multiplier") {}
    float GetValue(Action* action) override;
};

class MajordomoReflectionMultiplier : public Multiplier
{
public:
    MajordomoReflectionMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "majordomo reflection multiplier") {}
    float GetValue(Action* action) override;
};

// CoA: melee wait outside a Reflection of Shazzrah's Arcane Explosion until the tank drags Shazzrah clear of it
// CoA Combat (S1): Shazzrah's Arcane Force Nova (2105612, 10 s cast, 7,000-12,000 arcane to the whole raid) is the cast
// to interrupt. While he casts anything else, the bots keep their interrupts for it (Mass Counterspell, his 1 s casts).
// CoA Combat (A1): no area spell from a damage dealer while a Son of Flame is up (they merge when grouped).
class RagnarosSonsNoAoeMultiplier : public Multiplier
{
public:
    RagnarosSonsNoAoeMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "mc ragnaros sons no aoe") {}
    float GetValue(Action* action) override;
};
class ShazzrahSaveInterruptMultiplier : public Multiplier
{
public:
    ShazzrahSaveInterruptMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "mc shazzrah save interrupt") {}
    float GetValue(Action* action) override;
};
class ShazzrahReflectionMultiplier : public Multiplier
{
public:
    ShazzrahReflectionMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "shazzrah reflection multiplier") {}
    float GetValue(Action* action) override;
};

#endif
