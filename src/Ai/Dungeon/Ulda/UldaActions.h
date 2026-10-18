/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ULDAACTIONS_H
#define PLAYERBOTS_ULDAACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "UldaTriggers.h"

class UldaMoveFromGroundTremorAction : public DungeonBossMovementAction
{
public:
    UldaMoveFromGroundTremorAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "ulda move from ground tremor") {}
    bool Execute(Event event) override;
};

#endif
