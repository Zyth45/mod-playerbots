/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BWLTRIGGERS_H
#define PLAYERBOTS_BWLTRIGGERS_H

#include "Trigger.h"

// General

class BwlSuppressionDeviceTrigger : public Trigger
{
public:
    BwlSuppressionDeviceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl suppression device") {}
    bool IsActive() override;
};

// Razorgore the Untamed

class BwlRazorgoreNotMindControlledTrigger : public Trigger
{
public:
    BwlRazorgoreNotMindControlledTrigger(PlayerbotAI* botAI, std::string const& name = "bwl razorgore not mind controlled") : Trigger(botAI, name) {}
    bool IsActive() override;
};

// Vaelastrasz the Corrupt

class BwlVaelastraszPositioningTrigger : public Trigger
{
public:
    BwlVaelastraszPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl vaelastrasz positioning") {}
    bool IsActive() override;
};

class BwlVaelastraszBurningAdrenalineTrigger : public Trigger
{
public:
    BwlVaelastraszBurningAdrenalineTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl vaelastrasz burning adrenaline") {}
    bool IsActive() override;
};

// CoA Burning Adrenaline also cuts the threat its target generates by 100% (2110621 effect 2) and lands on the tank
// every other cast (every 40 s): the tank under it cannot hold Vaelastrasz, and he turns his breath and cleave onto
// whoever overtakes it. The first other tank in group order without it taunts him; also when he is on a non-tank.
class BwlVaelastraszTankSwapTrigger : public Trigger
{
public:
    BwlVaelastraszTankSwapTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl vaelastrasz tank swap") {}
    bool IsActive() override;
};

// Broodlord Lashlayer

class BwlBroodlordRangedTooCloseTrigger : public Trigger
{
public:
    BwlBroodlordRangedTooCloseTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl broodlord ranged too close") {}
    bool IsActive() override;
};

// Firemaw / Ebonroc / Flamegor

class BwlBlackDrakeNotVictimTrigger : public Trigger
{
public:
    BwlBlackDrakeNotVictimTrigger(PlayerbotAI* botAI, std::string bossName)
        : Trigger(botAI, "bwl " + bossName + " not victim"),
          bossName(std::move(bossName)) {}
    bool IsActive() override;

private:
    const std::string bossName;
};

// Chromaggus

class BwlAfflictionBronzeTrigger : public Trigger
{
public:
    BwlAfflictionBronzeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl affliction bronze") {}
    bool IsActive() override;
};

// Nefarian

// CoA Combat (F1 / E1): the drake's tank is marked (Firemaw: 8 Scorched; Ebonroc: Shadow of Ebonroc). The first other
// tank in group order that may take it taunts it.
class BwlDrakeTankSwapTrigger : public Trigger
{
public:
    BwlDrakeTankSwapTrigger(PlayerbotAI* botAI, std::string const name, std::string const bossName, bool firemaw)
        : Trigger(botAI, name), bossName(bossName), firemaw(firemaw) {}
    bool IsActive() override;

private:
    std::string const bossName;
    bool const firemaw;
};

// CoA Combat (C1): Chromaggus' breath is a 25 yd cone in front of him; everyone he does not target stands behind.
class BwlChromaggusPositioningTrigger : public Trigger
{
public:
    BwlChromaggusPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl chromaggus positioning") {}
    bool IsActive() override;
};

// CoA Combat (C2): a healer that can cure diseases sees a raid member with an unblessed Red affliction.
class BwlChromaggusRedAfflictionTrigger : public Trigger
{
public:
    BwlChromaggusRedAfflictionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl chromaggus red affliction") {}
    bool IsActive() override;
};

// CoA Combat (C4 / P1-a): standing in a Chromaggus patch or in Nefarius' Burning Ground.
class BwlGroundEffectTrigger : public Trigger
{
public:
    BwlGroundEffectTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl ground effect") {}
    bool IsActive() override;
};

// CoA Combat (G2): the bot carries Flamegor's Combustion with a raid member within its blast.
class BwlFlamegorCombustionTrigger : public Trigger
{
public:
    BwlFlamegorCombustionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl flamegor combustion") {}
    bool IsActive() override;
};

class BwlNefarianPositioningTrigger : public Trigger
{
public:
    BwlNefarianPositioningTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl nefarian positioning") {}
    bool IsActive() override;
};

class BwlNefarianWildMagicTrigger : public Trigger
{
public:
    BwlNefarianWildMagicTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl nefarian wild magic") {}
    bool IsActive() override;
};

// Trash

class BwlDeathTalonWyrmguardTankTrigger : public Trigger
{
public:
    BwlDeathTalonWyrmguardTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl death talon wyrmguard tank") {}
    bool IsActive() override;
};

class BwlDeathTalonWyrmguardRangedTrigger : public Trigger
{
public:
    BwlDeathTalonWyrmguardRangedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "bwl death talon wyrmguard ranged") {}
    bool IsActive() override;
};

#endif
