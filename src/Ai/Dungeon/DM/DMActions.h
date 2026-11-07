/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DMACTIONS_H
#define PLAYERBOTS_DMACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "DMTriggers.h"
#include "DungeonBossHelpers.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class DmLeaveMassiveGeyserAction : public DungeonBossMovementAction
{
public:
    DmLeaveMassiveGeyserAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "dm leave massive geyser") {}
    bool Execute(Event event) override;
};

class DmGeyserSpreadAction : public DungeonBossMovementAction
{
public:
    DmGeyserSpreadAction(PlayerbotAI* ai) : DungeonBossMovementAction(ai, "dm geyser spread") {}
    bool Execute(Event event) override;
};

class DmAttackHydrolingAction : public AttackAction
{
public:
    DmAttackHydrolingAction(PlayerbotAI* ai) : AttackAction(ai, "dm attack hydroling") {}
    bool Execute(Event event) override;
};

#endif
