/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCTRIGGERS_H
#define PLAYERBOTS_WCTRIGGERS_H

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Wailing Caverns (map 43).
namespace WailingCaverns
{
    // Skum's Overrun (2102624): he turns to a random player, roots for the 4 s cast, then charges 25 yd straight
    // ahead and tramples everyone within 4 yd of his path (spell_coa_skum_overrun: 400/1350/1750 damage and a
    // knockback).
    constexpr float OVERRUN_LENGTH = 25.0f;
    constexpr float OVERRUN_RADIUS = 4.0f;
    constexpr float OVERRUN_MARGIN = 1.5f;
    constexpr float OVERRUN_SIDE_STEP = 3.0f;

    bool IsOverrunning(Unit const* skum);

    // How far to either side of Skum's path the trample reaches for this bot.
    float OverrunHalfWidth(Unit const* skum, Unit const* bot);
    bool InOverrunPath(Unit const* skum, Unit const* bot);
}

class WcSkumOverrunTrigger : public Trigger
{
public:
    WcSkumOverrunTrigger(PlayerbotAI* ai) : Trigger(ai, "wc skum overrun") {}
    bool IsActive() override;
};

#endif
