#include "CoaFreshStartRules.h"

#include <cassert>

using namespace CoaFreshStart;

int main()
{
    int fast = 0, mass = 0, late = 0;
    for (uint64_t guid = 1; guid <= 10000; ++guid)
    {
        Pace const p = PaceOf(guid);
        assert(p == PaceOf(guid));  // always the same
        fast += p == Pace::Fast;
        mass += p == Pace::Mass;
        late += p == Pace::Late;
    }
    assert(fast > 1300 && fast < 1700 && mass > 6200 && mass < 6800 && late > 1800 && late < 2200);

    assert(XpPercent(Pace::Fast) == 150 && XpPercent(Pace::Mass) == 100 && XpPercent(Pace::Late) == 60);
    for (uint64_t guid = 1; guid <= 100; ++guid)
    {
        uint32_t const xp = ScaleXp(1000, guid);
        assert(xp == 1500 || xp == 1000 || xp == 600);
    }
    assert(ScaleXp(0, 7) == 0);
    return 0;
}
