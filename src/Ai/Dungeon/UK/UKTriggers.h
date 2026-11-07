/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_UKTRIGGERS_H
#define PLAYERBOTS_UKTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "DungeonStrategyUtils.h"
#include "GenericTriggers.h"
#include "PlayerbotAIConfig.h"
#include "Trigger.h"

enum UtgardeKeepIDs
{
    // Prince Keleseth
    SPELL_FROST_TOMB               = 48400,
    NPC_FROST_TOMB                 = 23965,

    // Ingvar the Plunderer
    SPELL_STAGGERING_ROAR_N         = 42708,
    SPELL_STAGGERING_ROAR_H         = 59708,
    SPELL_SMASH_N                   = 42669,
    SPELL_SMASH_H                   = 59706,
    SPELL_DREADFUL_ROAR_N           = 42729,
    SPELL_DREADFUL_ROAR_H           = 59734,
    SPELL_WOE_STRIKE_N              = 42730,
    SPELL_WOE_STRIKE_H              = 59735,
    SPELL_DARK_SMASH_N              = 42723,
    SPELL_DARK_SMASH_H              = 59709,
};

#define SPELL_STAGGERING_ROAR       DUNGEON_MODE(bot, SPELL_STAGGERING_ROAR_N, SPELL_STAGGERING_ROAR_H)
#define SPELL_DREADFUL_ROAR         DUNGEON_MODE(bot, SPELL_DREADFUL_ROAR_N, SPELL_DREADFUL_ROAR_H)
#define SPELL_SMASH                 DUNGEON_MODE(bot, SPELL_SMASH_N, SPELL_SMASH_H)
#define SPELL_DARK_SMASH            DUNGEON_MODE(bot, SPELL_DARK_SMASH_N, SPELL_DARK_SMASH_H)

class KelesethFrostTombTrigger : public Trigger
{
public:
    KelesethFrostTombTrigger(PlayerbotAI* ai) : Trigger(ai, "keleseth frost tomb") {}
    bool IsActive() override;
};

class DalronnDpsTrigger : public Trigger
{
public:
    DalronnDpsTrigger(PlayerbotAI* ai) : Trigger(ai, "dalronn dps") {}
    bool IsActive() override;
};

class IngvarDreadfulRoarTrigger : public Trigger
{
public:
    IngvarDreadfulRoarTrigger(PlayerbotAI* ai) : Trigger(ai, "ingvar dreadful roar") {}
    bool IsActive() override;
};

class IngvarSmashTankTrigger : public Trigger
{
public:
    IngvarSmashTankTrigger(PlayerbotAI* ai) : Trigger(ai, "ingvar smash tank") {}
    bool IsActive() override;
};

class IngvarSmashTankReturnTrigger : public Trigger
{
public:
    IngvarSmashTankReturnTrigger(PlayerbotAI* ai) : Trigger(ai, "ingvar smash tank return") {}
    bool IsActive() override;
};

class NotBehindIngvarTrigger : public Trigger
{
public:
    NotBehindIngvarTrigger(PlayerbotAI* ai) : Trigger(ai, "not behind ingvar") {}
    bool IsActive() override;
};

// Ingvar's roars (Staggering Roar in the first phase, Dreadful Roar with its stacking shadow vulnerability in the
// second) are 2 s casts over 60 yd that need line of sight. The ring of pillars beside his spawn point blocks it: a
// probe of 2026-10-22 found no cover within 7 yd of the spawn point and full cover from about 9 yd on toward the
// ring. The pillars are building geometry, not objects, so cover is tested the way the spell tests it, from the
// boss to the bot's chest with M2 models ignored, on spots the navmesh reaches.
namespace IngvarCover
{
    // 2 s at run speed, less a margin for the turn.
    constexpr float HIDE_MAX_PATH = 13.0f;
    // The ranged and healers stand where one step reaches cover.
    constexpr float READY_STEP = 3.5f;
    constexpr float READY_MAX_FROM_BOSS = 25.0f;
    constexpr float READY_MAX_PATH = 30.0f;
    // The tank holds Ingvar on his spawn point and stands on the side away from the pillar ring, so Smash goes away
    // from the group and the melee behind him are next to cover.
    constexpr float TANK_OFFSET = 3.0f;
    constexpr float TANK_TOLERANCE = 2.0f;

    Unit* Boss(PlayerbotAI* botAI, AiObjectContext* context);
    bool CastingRoar(Unit* boss);
    bool Hidden(Player* bot, Unit* boss, float x, float y, float z);
    // Seen by Ingvar, with cover one step away.
    bool NearCover(Player* bot, Unit* boss, float x, float y, float z);
    Position TankSpot(Unit* boss);
}

class IngvarRoarTrigger : public Trigger
{
public:
    IngvarRoarTrigger(PlayerbotAI* ai) : Trigger(ai, "ingvar roar") {}
    bool IsActive() override;
};

class IngvarCoverReadyTrigger : public Trigger
{
public:
    IngvarCoverReadyTrigger(PlayerbotAI* ai) : Trigger(ai, "ingvar cover ready", 2000) {}
    bool IsActive() override;
};

class IngvarTankSpotTrigger : public Trigger
{
public:
    IngvarTankSpotTrigger(PlayerbotAI* ai) : Trigger(ai, "ingvar tank spot") {}
    bool IsActive() override;
};

#endif
