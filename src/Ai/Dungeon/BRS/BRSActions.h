/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BRSACTIONS_H
#define PLAYERBOTS_BRSACTIONS_H

#include "Action.h"
#include "BRSTriggers.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class BrsMoveFromLineAttackAction : public DungeonBossMovementAction
{
public:
    BrsMoveFromLineAttackAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "brs move from line attack") {}
    bool Execute(Event event) override;
};

class BrsMoveFromBladestormAction : public DungeonBossMovementAction
{
public:
    BrsMoveFromBladestormAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "brs move from bladestorm") {}
    bool Execute(Event event) override;
};

class BrsMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    BrsMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "brs move from frontal cone") {}
    bool Execute(Event event) override;
};

#endif
