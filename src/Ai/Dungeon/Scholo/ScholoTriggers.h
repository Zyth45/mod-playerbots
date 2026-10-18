/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SCHOLOTRIGGERS_H
#define PLAYERBOTS_SCHOLOTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Scholomance (map 289).
namespace Scholomance
{
    using DungeonBoss::DegreesToRadians;

    // Lorekeeper Polkelt's Corrosive Acid (2100372): a 3 s cast at a random player, a 24 degree cone of 45 yd that
    // hits for a great part of a tank's health. The Ravenian's Mortal Cleave (2100193) and Lord Alexei Barov's
    // Shadow Cleave (2100356) are instant cones of 10 yd at the tank.
    constexpr DungeonBoss::FrontalCone FRONTAL_CONES[] = {
        { "lorekeeper polkelt", 45.0f, DegreesToRadians(12.0f), "Corrosive Acid" },
        { "the ravenian", 10.0f, DegreesToRadians(12.0f), nullptr },
        { "lord alexei barov", 10.0f, DegreesToRadians(12.0f), nullptr },
    };
}

class ScholoFrontalConeTrigger : public Trigger
{
public:
    ScholoFrontalConeTrigger(PlayerbotAI* ai) : Trigger(ai, "scholo frontal cone") {}
    bool IsActive() override;
};

#endif
