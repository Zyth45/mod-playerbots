/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COALEVELBRACKETRULES_H
#define PLAYERBOTS_COALEVELBRACKETRULES_H

#include <algorithm>
#include <cstdint>

// CoA 3.2 (a player's report of 20/11, the user's "a fix"): under the dynamic distribution
// (AiPlayerbot.LevelBrackets.Dynamic.UseDynamicDistribution) every bracket weighed the same, whatever its width: with
// the default brackets 50-54 and 55-59, levels 50-59 got two shares (25% of the bots instead of about 13%). A bracket
// now weighs its width in levels against ten, so two half brackets weigh one; the max level alone (60) keeps a full
// share. Pure rule.
namespace CoaLevelBracketRules
{
inline float WidthFactor(uint32_t lower, uint32_t upper, uint32_t maxLevel)
{
    if (upper < lower)
        return 0.0f;
    if (lower == upper && upper >= maxLevel)
        return 1.0f;  // the max level alone: a full share
    return std::max(0.1f, float(upper - lower + 1) / 10.0f);
}
}  // namespace CoaLevelBracketRules

#endif
