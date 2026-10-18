/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFDACTIONS_H
#define PLAYERBOTS_RFDACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RFDTriggers.h"

class RfdMoveFromExpelBloatAction : public DungeonBossMovementAction
{
public:
    RfdMoveFromExpelBloatAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "rfd move from expel bloat") {}
    bool Execute(Event event) override;
};

class RfdMoveFromBoneStormAction : public DungeonBossMovementAction
{
public:
    RfdMoveFromBoneStormAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "rfd move from bone storm") {}
    bool Execute(Event event) override;
};

class RfdMoveFromMarkedAction : public DungeonBossMovementAction
{
public:
    RfdMoveFromMarkedAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "rfd move from marked player") {}
    bool Execute(Event event) override;
};

#endif
