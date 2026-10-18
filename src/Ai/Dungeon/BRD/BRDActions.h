/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRDACTIONS_H
#define PLAYERBOTS_BRDACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "BRDTriggers.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class BrdMoveFromGoutAction : public DungeonBossMovementAction
{
public:
    BrdMoveFromGoutAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "brd move from gout of flame") {}
    bool Execute(Event event) override;
};

class BrdAttackBurningSpiritAction : public AttackAction
{
public:
    BrdAttackBurningSpiritAction(PlayerbotAI* ai) : AttackAction(ai, "brd attack burning spirit") {}
    bool Execute(Event event) override;
};

class BrdAttackThaurissanAction : public AttackAction
{
public:
    BrdAttackThaurissanAction(PlayerbotAI* ai) : AttackAction(ai, "brd attack thaurissan") {}
    bool Execute(Event event) override;
};

class BrdMoveFromGroundTremorAction : public DungeonBossMovementAction
{
public:
    BrdMoveFromGroundTremorAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "brd move from ground tremor") {}
    bool Execute(Event event) override;
};

class BrdMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    BrdMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "brd move from frontal cone") {}
    bool Execute(Event event) override;
};

#endif
