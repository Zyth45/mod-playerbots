/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCACTIONS_H
#define PLAYERBOTS_WCACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "WCTriggers.h"

class WcMoveFromOverrunAction : public DungeonBossMovementAction
{
public:
    WcMoveFromOverrunAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "wc move from overrun") {}
    bool Execute(Event event) override;
};

#endif
