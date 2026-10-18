/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MARAACTIONS_H
#define PLAYERBOTS_MARAACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "MaraTriggers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class MaraShareBoulderAction : public DungeonBossMovementAction
{
public:
    MaraShareBoulderAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "mara share boulder") {}
    bool Execute(Event event) override;
};

class MaraMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    MaraMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "mara move from frontal cone") {}
    bool Execute(Event event) override;
};

class MaraMoveFromDustFieldAction : public DungeonBossMovementAction
{
public:
    MaraMoveFromDustFieldAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "mara move from dust field") {}
    bool Execute(Event event) override;
};

#endif
