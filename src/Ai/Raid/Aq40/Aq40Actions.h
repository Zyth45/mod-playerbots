/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40ACTIONS_H
#define PLAYERBOTS_AQ40ACTIONS_H

#include "AttackAction.h"
#include "MovementActions.h"

class Aq40CthunLeaveStomachAction : public MovementAction
{
public:
    Aq40CthunLeaveStomachAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun leave stomach") {}
    bool Execute(Event event) override;

private:
    uint32 _lastExitAttemptMs = 0;
};

class Aq40CthunDodgeGlareAction : public MovementAction
{
public:
    Aq40CthunDodgeGlareAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun dodge glare") {}
    bool Execute(Event event) override;
};

class Aq40CthunLookAwayAction : public Action
{
public:
    Aq40CthunLookAwayAction(PlayerbotAI* botAI) : Action(botAI, "aq40 cthun look away") {}
    bool Execute(Event event) override;
};

class Aq40CthunAvoidDevourAction : public MovementAction
{
public:
    Aq40CthunAvoidDevourAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun avoid devour") {}
    bool Execute(Event event) override;
};

class Aq40CthunSpitOutAction : public Action
{
public:
    Aq40CthunSpitOutAction(PlayerbotAI* botAI) : Action(botAI, "aq40 cthun spit out") {}
    bool Execute(Event event) override;

private:
    uint32 _lastAttemptMs = 0;
};

class Aq40CthunMoveToSpotAction : public MovementAction
{
public:
    Aq40CthunMoveToSpotAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 cthun move to spot") {}
    bool Execute(Event event) override;
};

class Aq40CthunAttackAction : public AttackAction
{
public:
    Aq40CthunAttackAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 cthun attack") {}
    bool Execute(Event event) override;
};

class Aq40SkeramAttackAction : public AttackAction
{
public:
    Aq40SkeramAttackAction(PlayerbotAI* botAI) : AttackAction(botAI, "aq40 skeram attack") {}
    bool Execute(Event event) override;
};

class Aq40SkeramStopAttackAction : public Action
{
public:
    Aq40SkeramStopAttackAction(PlayerbotAI* botAI) : Action(botAI, "aq40 skeram stop attack") {}
    bool Execute(Event event) override;
};

class Aq40SkeramAvoidPsychicShockAction : public MovementAction
{
public:
    Aq40SkeramAvoidPsychicShockAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 skeram avoid psychic shock")
    {
    }
    bool Execute(Event event) override;
};

class Aq40SkeramGazeSpreadAction : public MovementAction
{
public:
    Aq40SkeramGazeSpreadAction(PlayerbotAI* botAI) : MovementAction(botAI, "aq40 skeram gaze spread") {}
    bool Execute(Event event) override;
};

#endif
