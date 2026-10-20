/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFCACTIONS_H
#define PLAYERBOTS_RFCACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RFCTriggers.h"

class AttackOggleflintAddAction : public AttackAction
{
public:
    AttackOggleflintAddAction(PlayerbotAI* ai) : AttackAction(ai, "attack oggleflint add") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class AttackJergoshAddAction : public AttackAction
{
public:
    AttackJergoshAddAction(PlayerbotAI* ai) : AttackAction(ai, "attack jergosh add") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

class AttackBazzalanAction : public AttackAction
{
public:
    AttackBazzalanAction(PlayerbotAI* ai) : AttackAction(ai, "attack bazzalan") {}
    bool Execute(Event event) override;
};

class TaragamanAvoidFireNovaAction : public MovementAction
{
public:
    TaragamanAvoidFireNovaAction(PlayerbotAI* ai) : MovementAction(ai, "avoid fire nova") {}
    bool Execute(Event event) override;
};

class TaragamanHideBehindPillarAction : public MovementAction
{
public:
    TaragamanHideBehindPillarAction(PlayerbotAI* ai) : MovementAction(ai, "hide behind earth pillar") {}
    bool Execute(Event event) override;
};

#endif
