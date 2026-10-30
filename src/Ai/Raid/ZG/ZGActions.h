/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGACTIONS_H
#define PLAYERBOTS_ZGACTIONS_H

#include "Action.h"
#include "MovementActions.h"

class BloodlordMandokirStopCausingThreatAction : public Action
{
public:
    BloodlordMandokirStopCausingThreatAction(PlayerbotAI* botAI)
        : Action(botAI, "bloodlord mandokir stop causing threat")
    {
    }

    bool Execute(Event event) override;
};

// Hakkar: out of Blood Scythe's cone to his nearer flank, at the same distance (melee stay in melee).
class HakkarAvoidBloodScytheAction : public MovementAction
{
public:
    HakkarAvoidBloodScytheAction(PlayerbotAI* botAI) : MovementAction(botAI, "hakkar avoid blood scythe") {}
    bool Execute(Event event) override;
};

#endif
