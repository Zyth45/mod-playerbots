// CoA 3.2: an enchanter bot enchants its own gear (CoaEnchantSelfRules.h).
#include "CoaEnchantSelfRules.h"

#include <cassert>

using namespace CoaEnchantSelfRules;

int main()
{
    // Never a better or an equal enchantment replaced.
    assert(Replaces(12.0f, 0.0f));   // a bare piece
    assert(Replaces(12.0f, 8.0f));   // a weaker one there
    assert(!Replaces(8.0f, 12.0f));  // a better one there
    assert(!Replaces(12.0f, 12.0f)); // the same
    // The enchantment's limits.
    assert(Usable(40, 0, 0, 0, 0));
    assert(Usable(40, 35, 0, 0, 0));
    assert(!Usable(30, 35, 0, 0, 0));      // too low for it
    assert(!Usable(60, 0, 202, 150, 225)); // a skill rank short
    assert(Usable(60, 0, 202, 225, 225));
    // 3.2.2: the tools asked for must be held (a runed rod), none asked: always fine.
    assert(HasTools(false, false, false, false));
    assert(HasTools(true, true, false, false));
    assert(!HasTools(true, false, false, false)); // no rod
    assert(!HasTools(true, true, true, false));
    // 3.2.2: where it studies.
    assert(StudyPlace(true, false, false, false));  // a capital, always
    assert(!StudyPlace(false, false, true, true));  // RestAreas off: a capital only
    assert(StudyPlace(false, true, true, false));   // a city it rests in
    assert(StudyPlace(false, true, false, true));   // an inn
    assert(!StudyPlace(false, true, false, false)); // out in the wild
    // 3.2.2: one stack of each reagent kept, the rest sold.
    assert(KeepsReagent(0, 20));
    assert(KeepsReagent(20, 20));
    assert(!KeepsReagent(21, 20));
    assert(KeepsReagent(1, 0)); // an unstackable reagent: one kept
    assert(!KeepsReagent(2, 0));
    // 3.2.2: disenchant or sell.
    assert(Disenchants(false, true, false, false));   // dust worth more: always
    assert(!Disenchants(false, false, false, true));  // a green, reagents in hand: sold
    assert(Disenchants(false, false, true, true));    // a green, short of reagents: disenchanted
    assert(!Disenchants(false, false, true, false));  // a blue or an epic, short: still sold
    assert(Disenchants(true, false, false, false));   // a bound piece it replaced: always
    // 3.2.2: one cause per try, the furthest step reached.
    assert(Cause(false, false, false, false, false) == Refusal::NoRecipe);
    assert(Cause(true, true, true, true, true) == Refusal::NoGain);   // a piece looked at: no gain
    assert(Cause(true, false, true, true, true) == Refusal::NoGold);
    assert(Cause(true, false, false, true, true) == Refusal::NoReagent);
    assert(Cause(true, false, false, false, true) == Refusal::NoRod);
    return 0;
}
