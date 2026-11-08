/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MCTRIGGERS_H
#define PLAYERBOTS_MCTRIGGERS_H

#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Trigger.h"
#include "PlayerbotAIConfig.h"
#include "RaidBossScripts.h"

// CoA Combat (lot 7, docs/MC-ETAT-3.0.md): every Molten Core fix of the session is off unless
// AiPlayerbot.CoaCombat.MoltenCore.Enable is 1.
inline bool McCoaFixesOn() { return sPlayerbotAIConfig.coaCombatMoltenCoreEnable; }

// A kill order the session added (Lucifron, Sulfuron): the stock skull logic, behind the option.
class McCoaKillOrderTrigger : public RaidKillOrderMarkTrigger
{
public:
    McCoaKillOrderTrigger(PlayerbotAI* botAI, std::string const name, std::string const bossName)
        : RaidKillOrderMarkTrigger(botAI, name, bossName) {}
    bool IsActive() override { return McCoaFixesOn() && RaidKillOrderMarkTrigger::IsActive(); }
};

class McLivingBombDebuffTrigger : public Trigger
{
public:
    McLivingBombDebuffTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc living bomb debuff") {}
    bool IsActive() override;
};

class McBaronGeddonInfernoTrigger : public Trigger
{
public:
    McBaronGeddonInfernoTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc baron geddon inferno") {}
    bool IsActive() override;
};

class McShazzrahRangedTrigger : public Trigger
{
public:
    McShazzrahRangedTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc shazzrah ranged") {}
    bool IsActive() override;
};

// CoA: a non-tank stands in a Reflection of Shazzrah's Arcane Explosion
class McShazzrahReflectionTrigger : public Trigger
{
public:
    McShazzrahReflectionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc shazzrah reflection") {}
    bool IsActive() override;
};

// CoA: Shazzrah, held by this tank, stands in a Reflection's Arcane Explosion
class McShazzrahTankReflectionTrigger : public Trigger
{
public:
    McShazzrahTankReflectionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc shazzrah tank reflection") {}
    bool IsActive() override;
};

// CoA: Sacrificial Chains holds raid members while it lives
class McSacrificialChainsTrigger : public Trigger
{
public:
    McSacrificialChainsTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc sacrificial chains") {}
    bool IsActive() override;
};

class McGolemaggMarkBossTrigger : public Trigger
{
public:
    McGolemaggMarkBossTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg mark boss") {}
    bool IsActive() override;
};

class McGolemaggIsMainTankTrigger : public Trigger
{
public:
    McGolemaggIsMainTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg is main tank") {}
    bool IsActive() override;
};

class McGolemaggIsAssistTankTrigger : public Trigger
{
public:
    McGolemaggIsAssistTankTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg is assist tank") {}
    bool IsActive() override;
};

class McGolemaggIsHealerTrigger : public Trigger
{
public:
    McGolemaggIsHealerTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg is healer") {}
    bool IsActive() override;
};

// Gehennas is casting Conjure Flame Orb and the bot is within the orb's reach of him
class McGehennasFlameOrbCastTrigger : public Trigger
{
public:
    McGehennasFlameOrbCastTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc gehennas flame orb cast") {}
    bool IsActive() override;
};

// Standing in Gehennas' Rain of Fire (stock or CoA)
class McGehennasRainOfFireTrigger : public Trigger
{
public:
    McGehennasRainOfFireTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc gehennas rain of fire") {}
    bool IsActive() override;
};

// Standing in one of Magmadar's fire patches (stock trap or CoA heads' ground fire)
class McMagmadarLavaBombTrigger : public Trigger
{
public:
    McMagmadarLavaBombTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc magmadar lava bomb") {}
    bool IsActive() override;
};

class McCoreHoundMarkTrigger : public Trigger
{
public:
    McCoreHoundMarkTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc core hound mark") {}
    bool IsActive() override;
};

// Standing/swimming in magma (knocked into a lava pool by Ragnaros' Wrath or an Eruption)
class McInLavaTrigger : public Trigger
{
public:
    McInLavaTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc in lava") {}
    bool IsActive() override;
};

class McGolemaggMagmaSplashTrigger : public Trigger
{
public:
    McGolemaggMagmaSplashTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc golemagg magma splash") {}
    bool IsActive() override;
};

class McMajordomoDamageShieldTrigger : public Trigger
{
public:
    McMajordomoDamageShieldTrigger(PlayerbotAI* botAI) : Trigger(botAI, "mc majordomo damage shield") {}
    bool IsActive() override;
};

#endif
