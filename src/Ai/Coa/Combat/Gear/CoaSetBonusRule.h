/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_SET_BONUS_RULE_H
#define PLAYERBOTS_COA_SET_BONUS_RULE_H

#include <cstdint>
#include <vector>

// What a set piece is worth on top of its own stats (CoA Combat, lot 6), with no game types for the unit test.
//
// A piece is worth the bonuses it UNLOCKS given the other pieces of its set the bot already wears in the other slots:
// with `others` pieces worn elsewhere, wearing this one makes `others + 1`, so it brings exactly the bonuses whose
// threshold is `others + 1`. Scored this way on both sides of a comparison (the candidate and the item it would
// replace), swapping a set piece for a stronger loose item costs the bonus it breaks, and the piece that completes a
// bonus is worth that bonus.
namespace CoaSetBonusRule
{
    struct Bonus
    {
        std::uint32_t threshold;  // pieces worn for the bonus to apply
        float value;              // its weight, in the unit of the item scores
    };

    inline float Marginal(std::uint32_t othersWorn, std::vector<Bonus> const& bonuses)
    {
        float sum = 0.0f;
        for (Bonus const& b : bonuses)
            if (b.threshold == othersWorn + 1)
                sum += b.value;
        return sum;
    }
}

#endif
