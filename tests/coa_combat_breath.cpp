// The breath guard's clock (src/Ai/Coa/Combat/Breath/CoaBreathRule.h).
#include "Ai/Coa/Combat/Breath/CoaBreathRule.h"

#include <cassert>
#include <cstdint>

using CoaBreathRule::Step;
using CoaBreathRule::UNDER_WATER_LIMIT_MS;

int main()
{
    // 89 s under water out of combat: not yet; the 90th second brings it up and restarts the count.
    std::uint32_t ms = 0;
    for (int s = 1; s < 90; ++s)
        assert(!Step(ms, 1000, true, false));
    assert(Step(ms, 1000, true, false));
    assert(ms == 0);

    // Surfacing for one tick restarts the count.
    ms = 0;
    for (int s = 0; s < 80; ++s)
        Step(ms, 1000, true, false);
    assert(!Step(ms, 1000, false, false));
    assert(ms == 0);
    for (int s = 1; s < 90; ++s)
        assert(!Step(ms, 1000, true, false));

    // A fight under water is left alone and restarts the count too.
    ms = UNDER_WATER_LIMIT_MS - 1;
    assert(!Step(ms, 1000, true, true));
    assert(ms == 0);

    // Uneven sweeps add up.
    ms = 0;
    assert(!Step(ms, 60000, true, false));
    assert(Step(ms, 30000, true, false));
    return 0;
}
