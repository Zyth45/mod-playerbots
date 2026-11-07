/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_UKACTIONS_H
#define PLAYERBOTS_UKACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "UKTriggers.h"

class AttackFrostTombAction : public AttackAction
{
public:
    AttackFrostTombAction(PlayerbotAI* ai) : AttackAction(ai, "attack frost tomb") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class AttackDalronnAction : public AttackAction
{
public:
    AttackDalronnAction(PlayerbotAI* ai) : AttackAction(ai, "attack dalronn") {}
    bool Execute(Event event) override;
};

class IngvarDodgeSmashAction : public MovementAction
{
public:
    IngvarDodgeSmashAction(PlayerbotAI* ai) : MovementAction(ai, "ingvar dodge smash") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class IngvarSmashReturnAction : public MovementAction
{
public:
    IngvarSmashReturnAction(PlayerbotAI* ai) : MovementAction(ai, "ingvar smash return") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class IngvarHideFromRoarAction : public DungeonBossMovementAction
{
public:
    IngvarHideFromRoarAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "ingvar hide from roar") {}
    bool Execute(Event event) override;
};

class IngvarCoverReadyAction : public DungeonBossMovementAction
{
public:
    IngvarCoverReadyAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "ingvar cover ready") {}
    bool Execute(Event event) override;
};

class IngvarHoldSpotAction : public MovementAction
{
public:
    IngvarHoldSpotAction(PlayerbotAI* ai) : MovementAction(ai, "ingvar hold spot") {}
    bool Execute(Event event) override;
};

#endif
