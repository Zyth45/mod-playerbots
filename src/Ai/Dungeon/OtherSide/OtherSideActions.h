/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_OTHERSIDEACTIONS_H
#define PLAYERBOTS_OTHERSIDEACTIONS_H

#include "AttackAction.h"
#include "MovementActions.h"

class OtherSideLeaveHazardAction : public MovementAction
{
public:
    OtherSideLeaveHazardAction(PlayerbotAI* botAI) : MovementAction(botAI, "other side leave hazard") {}
    bool Execute(Event event) override;
};

class OtherSideKickEggAction : public MovementAction
{
public:
    OtherSideKickEggAction(PlayerbotAI* botAI) : MovementAction(botAI, "other side kick egg") {}
    bool Execute(Event event) override;
};

class OtherSideRunAroundAction : public MovementAction
{
public:
    OtherSideRunAroundAction(PlayerbotAI* botAI) : MovementAction(botAI, "other side run around") {}
    bool Execute(Event event) override;
};

class OtherSideCloseInOnMuzahAction : public MovementAction
{
public:
    OtherSideCloseInOnMuzahAction(PlayerbotAI* botAI) : MovementAction(botAI, "other side close in on muzah") {}
    bool Execute(Event event) override;
};

class OtherSideAttackSpiderAction : public AttackAction
{
public:
    OtherSideAttackSpiderAction(PlayerbotAI* botAI) : AttackAction(botAI, "other side attack spider") {}
    bool Execute(Event event) override;
};

#endif
