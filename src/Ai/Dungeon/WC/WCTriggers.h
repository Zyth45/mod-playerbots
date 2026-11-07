/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WCTRIGGERS_H
#define PLAYERBOTS_WCTRIGGERS_H

#include <optional>

#include "DungeonBossHelpers.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Wailing Caverns (map 43).
namespace WailingCaverns
{
    // Skum's Overrun (2102624): he turns to a random player, roots for the 3 s cast, then charges up to 25 yd
    // straight ahead, stopping at the first obstacle, and tramples everyone within 4 yd of himself for 1.75 s
    // (spell_coa_skum_overrun: 400/1350/1750 damage and a 12 yd knockback). He stands in a pen of wooden stakes
    // against the cave wall, so a plain side step often ends on a stake or in the rock.
    constexpr float OVERRUN_LENGTH = 25.0f;
    constexpr float OVERRUN_RADIUS = 4.0f;
    constexpr float OVERRUN_MARGIN = 2.5f;
    // The trample runs on after the cast; the danger lasts until it has stopped.
    constexpr uint32 OVERRUN_CHARGE_MS = 2200;

    struct OverrunLine
    {
        float x = 0.0f;
        float y = 0.0f;
        float angle = 0.0f;
    };

    // The line of the Overrun Skum is casting or still running, remembered from the moment the cast began.
    std::optional<OverrunLine> ActiveOverrun(Unit* skum);

    // How far to either side of Skum's line the trample reaches for this bot.
    float OverrunHalfWidth(Unit const* skum, Unit const* bot);
    bool InOverrunPath(OverrunLine const& line, float halfWidth, float x, float y);
}

class WcSkumOverrunTrigger : public Trigger
{
public:
    WcSkumOverrunTrigger(PlayerbotAI* ai) : Trigger(ai, "wc skum overrun") {}
    bool IsActive() override;
};

#endif
