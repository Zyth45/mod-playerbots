// CoA 3.3.1: the formulas' pure rules (CoaFormulaRules.h).
#include "CoaFormulaRules.h"

#include <cassert>
#include <cstdio>

using namespace CoaFormulaRules;

int main()
{
    // Classic format: generic learn + the taught spell (Plans: Lionheart Helm).
    auto none = [](uint32_t) -> uint32_t { return 0; };
    uint32_t const plans[5] = {483, 16729, 0, 0, 0};
    assert(TaughtSpell(plans, 5, none) == 16729);
    uint32_t const wotlk[5] = {55884, 0, 12345, 0, 0};
    assert(TaughtSpell(wotlk, 5, none) == 12345);

    // CoA format: one on-use learner whose LEARN_SPELL effect gives the taught spell (Crusader: 113065 -> 20034).
    auto learner = [](uint32_t id) -> uint32_t { return id == 113065 ? 20034 : 0; };
    uint32_t const crusader[5] = {113065, 0, 0, 0, 0};
    assert(TaughtSpell(crusader, 5, learner) == 20034);

    // No spell: no recipe.
    uint32_t const empty[5] = {0, 0, 0, 0, 0};
    assert(TaughtSpell(empty, 5, learner) == 0);
    uint32_t const onlyLearn[5] = {483, 0, 0, 0, 0};
    assert(TaughtSpell(onlyLearn, 5, learner) == 0);

    // Learnable: every condition.
    assert(Learnable(20034, false, true, true, true));
    assert(!Learnable(0, false, true, true, true));
    assert(!Learnable(20034, true, true, true, true));   // known
    assert(!Learnable(20034, false, false, true, true)); // not its profession
    assert(!Learnable(20034, false, true, false, true)); // rank or level too low
    assert(!Learnable(20034, false, true, true, false)); // reputation too low

    // Daily rare formula.
    assert(DrawsFormula(225, 225, 24, 25));
    assert(!DrawsFormula(225, 225, 25, 25));
    assert(!DrawsFormula(224, 225, 0, 25));
    assert(!DrawsFormula(300, 225, 0, 0));

    // Purchase cap.
    assert(BuyCap(1000000, 25, 60, 10000) == 250000);
    assert(BuyCap(10000000, 25, 60, 10000) == 600000);
    assert(BuyCap(1000, 250, 60, 10000) == 1000);
    assert(BuyCap(0, 25, 60, 10000) == 0);

    // Players first: a player's listing waits a day, a bot's does not.
    assert(ListingOpenToBots(true, 1000, 1000, 86400));
    assert(!ListingOpenToBots(false, 1000, 1000 + 86399, 86400));
    assert(ListingOpenToBots(false, 1000, 1000 + 86400, 86400));
    assert(!ListingOpenToBots(false, 2000, 1000, 86400));  // clock behind: wait

    std::puts("coa_formula_rules: OK");
    return 0;
}
