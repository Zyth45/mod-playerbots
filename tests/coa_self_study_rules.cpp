// CoA 3.2: the hourly self study of a bot in a capital (CoaSelfStudyRules.h).
#include "CoaSelfStudyRules.h"

#include <cassert>

using namespace CoaSelfStudyRules;

int main()
{
    // The cap: a share of the gold, no more than the level allows.
    assert(SpendCap(1000000, 10, 60, 2500) == 100000);  // 100g held: 10g, under 60 * 25s
    assert(SpendCap(100000000, 10, 60, 2500) == 150000); // rich: the level's 15g
    assert(SpendCap(0, 10, 60, 2500) == 0);
    // Worth buying: 5 % over what it replaces, any gain on an empty slot.
    assert(WorthBuying(6.0f, 100.0f, 5));
    assert(WorthBuying(5.0f, 100.0f, 5));
    assert(!WorthBuying(4.0f, 100.0f, 5));
    assert(WorthBuying(0.5f, 0.0f, 5));
    assert(!WorthBuying(0.0f, 0.0f, 5));
    // A vendor first, the auction house next, nothing when neither has it.
    assert(UnitPrice(40, 300) == 40);
    assert(UnitPrice(0, 300) == 300);
    assert(UnitPrice(0, 0) == 0);
    // The first study within the first interval.
    assert(FirstStudyDelay(0.0f, 3600) == 0);
    assert(FirstStudyDelay(0.5f, 3600) == 1800);
    assert(FirstStudyDelay(1.0f, 3600) < 3600);
    return 0;
}
