/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_BREATH_RULE_H
#define PLAYERBOTS_COA_BREATH_RULE_H

#include <cstdint>

// The breath guard's clock (CoA Combat, guard B2), with no game types so the unit test builds it alone.
//
// A bot under water and out of combat for half its 180 s breath bar is brought back up; a fight under water is left
// alone (its own movers own the bot), and surfacing for one tick restarts the count.
namespace CoaBreathRule
{
    inline constexpr std::uint32_t UNDER_WATER_LIMIT_MS = 90 * 1000;

    // Advances one bot's under-water time by `diffMs` and says whether to bring it up now. `underWaterMs` is the bot's
    // own counter; it goes back to zero on the surface, in combat, and once the bot has been brought up.
    inline bool Step(std::uint32_t& underWaterMs, std::uint32_t diffMs, bool underWater, bool inCombat)
    {
        if (!underWater || inCombat)
        {
            underWaterMs = 0;
            return false;
        }
        underWaterMs += diffMs;
        if (underWaterMs < UNDER_WATER_LIMIT_MS)
            return false;
        underWaterMs = 0;
        return true;
    }
}

#endif
