// CoA 3.2: under the dynamic distribution, a level bracket weighs its width (CoaLevelBracketRules.h).
#include "CoaLevelBracketRules.h"

#include <cassert>
#include <cmath>

using CoaLevelBracketRules::WidthFactor;

int main()
{
    // The default brackets: 1-9, 10-19 ... 40-49, 50-54, 55-59, 60.
    assert(std::fabs(WidthFactor(1, 9, 60) - 0.9f) < 1e-4f);
    assert(std::fabs(WidthFactor(10, 19, 60) - 1.0f) < 1e-4f);
    float const half = WidthFactor(50, 54, 60);
    assert(std::fabs(half - 0.5f) < 1e-4f);
    assert(std::fabs(half + WidthFactor(55, 59, 60) - 1.0f) < 1e-4f);  // 50-59 weighs one band, not two
    assert(std::fabs(WidthFactor(60, 60, 60) - 1.0f) < 1e-4f);          // the max level alone: a full share
    // Shares with equal real players: 50-59 gets 1/6.9 of the bots (14.5%), no more a quarter (2/8).
    float total = 0.9f + 1.0f * 4 + 0.5f + 0.5f + 1.0f;
    assert((half * 2) / total > 0.14f && (half * 2) / total < 0.15f);
    assert(WidthFactor(30, 20, 60) == 0.0f);  // an empty bracket
    assert(WidthFactor(5, 5, 60) > 0.0f);     // a one-level bracket below the max still counts a little
    return 0;
}
