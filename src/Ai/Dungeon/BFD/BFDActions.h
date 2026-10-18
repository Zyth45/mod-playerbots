/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BFDACTIONS_H
#define PLAYERBOTS_BFDACTIONS_H

#include "Action.h"
#include "BFDTriggers.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class BfdMoveFromTorrentAction : public DungeonBossMovementAction
{
public:
    BfdMoveFromTorrentAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "bfd move from torrent") {}
    bool Execute(Event event) override;
};

class BfdMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    BfdMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "bfd move from frontal cone") {}
    bool Execute(Event event) override;
};

#endif
