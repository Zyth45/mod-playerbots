// The set bonus a piece unlocks (src/Ai/Coa/Combat/Gear/CoaSetBonusRule.h).
#include "Ai/Coa/Combat/Gear/CoaSetBonusRule.h"

#include <cassert>

using CoaSetBonusRule::Bonus;
using CoaSetBonusRule::Marginal;

int main()
{
    // A tier set: 3, 5 and 8 pieces.
    std::vector<Bonus> const tier = {{3, 30.0f}, {5, 50.0f}, {8, 80.0f}};
    assert(Marginal(0, tier) == 0.0f);   // the first piece unlocks nothing
    assert(Marginal(1, tier) == 0.0f);
    assert(Marginal(2, tier) == 30.0f);  // the third piece brings the 3-piece bonus
    assert(Marginal(4, tier) == 50.0f);  // the fifth, the 5-piece one
    assert(Marginal(7, tier) == 80.0f);
    assert(Marginal(8, tier) == 0.0f);   // a ninth piece in another slot adds nothing

    // Two bonuses on the same threshold add up; no bonus at all, nothing.
    assert(Marginal(1, {{2, 10.0f}, {2, 5.0f}}) == 15.0f);
    assert(Marginal(3, {}) == 0.0f);
    return 0;
}