/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SCHOLOACTIONS_H
#define PLAYERBOTS_SCHOLOACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScholoTriggers.h"

class ScholoMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    ScholoMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "scholo move from frontal cone") {}
    bool Execute(Event event) override;
};

#endif
