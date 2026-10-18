/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_STRATACTIONS_H
#define PLAYERBOTS_STRATACTIONS_H

#include "Action.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "StratTriggers.h"

class StratMoveFromCrusadersHammerAction : public DungeonBossMovementAction
{
public:
    StratMoveFromCrusadersHammerAction(PlayerbotAI* ai)
        : DungeonBossMovementAction(ai, "strat move from crusader's hammer") {}
    bool Execute(Event event) override;
};

class StratMoveFromFrontalConeAction : public DungeonBossMovementAction
{
public:
    StratMoveFromFrontalConeAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "strat move from frontal cone") {}
    bool Execute(Event event) override;
};

class StratMoveFromRetributionAction : public DungeonBossMovementAction
{
public:
    StratMoveFromRetributionAction(PlayerbotAI* ai)
        : DungeonBossMovementAction(ai, "strat move from divine retribution") {}
    bool Execute(Event event) override;
};

#endif
