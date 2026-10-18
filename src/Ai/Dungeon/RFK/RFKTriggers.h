/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFKTRIGGERS_H
#define PLAYERBOTS_RFKTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Razorfen Kraul (map 47).
namespace RazorfenKraul
{
    using DungeonBoss::DegreesToRadians;

    // Death Speaker Jargba's Shadow Bolt Volley (2102781): a 3 s cast at the tank, a 24 degree cone of 50 yd.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "death speaker jargba", 50.0f, DegreesToRadians(12.0f), "Shadow Bolt Volley" },
    };
}

class RfkFrontalConeTrigger : public Trigger
{
public:
    RfkFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "rfk frontal cone") {}
    bool IsActive() override;
};

#endif
