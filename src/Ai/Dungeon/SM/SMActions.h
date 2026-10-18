/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMACTIONS_H
#define PLAYERBOTS_SMACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SMTriggers.h"

class SmMoveFromDoanAction : public MovementAction
{
public:
    SmMoveFromDoanAction(PlayerbotAI* ai) : MovementAction(ai, "sm move from doan") {}
    bool Execute(Event event) override;
};

class SmDoanRangedAction : public MovementAction
{
public:
    SmDoanRangedAction(PlayerbotAI* ai) : MovementAction(ai, "sm doan ranged keep away") {}
    bool Execute(Event event) override;
};

class SmMoveFromHerodAction : public MovementAction
{
public:
    SmMoveFromHerodAction(PlayerbotAI* ai) : MovementAction(ai, "sm move from herod") {}
    bool Execute(Event event) override;
};

class SmAttackWhitemaneAction : public AttackAction
{
public:
    SmAttackWhitemaneAction(PlayerbotAI* ai) : AttackAction(ai, "sm attack whitemane") {}
    bool Execute(Event event) override;
};

#endif
