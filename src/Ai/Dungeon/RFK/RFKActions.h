/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFKACTIONS_H
#define PLAYERBOTS_RFKACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RFKTriggers.h"

class RfkMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    RfkMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "rfk move from frontal cone") {}
    bool Execute(Event event) override;
};

#endif
