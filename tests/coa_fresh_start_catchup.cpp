#include "CoaFreshStartRules.h"

#include <cassert>

using namespace CoaFreshStart;

int main()
{
    CatchUpSettings s;
    // The front: median of the active humans' best characters, none under MinHumans.
    assert(MedianFront({}, 2) == 0);
    assert(MedianFront({40}, 2) == 0);
    assert(MedianFront({40}, 1) == 40);
    assert(MedianFront({60, 20}, 2) == 20);  // lower middle: one rushing player does not pull it
    assert(MedianFront({60, 30, 25}, 2) == 30);
    assert(MedianFront({60, 59, 12, 11, 10}, 2) == 12);

    // The bonus: none within the margin or ahead, +25 % a level beyond, at most x4.
    assert(CatchUpPercent(10, 0, s) == 100);
    assert(CatchUpPercent(30, 30, s) == 100);
    assert(CatchUpPercent(35, 30, s) == 100);  // ahead: never slowed
    assert(CatchUpPercent(27, 30, s) == 100);  // 3 behind: the margin
    assert(CatchUpPercent(26, 30, s) == 125);
    assert(CatchUpPercent(20, 30, s) == 275);
    assert(CatchUpPercent(1, 60, s) == 400);

    // Pace x bonus, under the total cap with RandomBotXPRate, never under the pace.
    uint64_t fast = 0, late = 0;
    for (uint64_t g = 1; !fast || !late; ++g)
    {
        if (!fast && PaceOf(g) == Pace::Fast)
            fast = g;
        if (!late && PaceOf(g) == Pace::Late)
            late = g;
    }
    assert(FreshStartPercent(fast, 20, 30, false, s, 200) == 150);
    assert(FreshStartPercent(fast, 28, 30, true, s, 200) == 150);  // no bonus: the pace
    assert(FreshStartPercent(late, 20, 30, true, s, 100) == 165);  // 60 % x 275 %
    assert(FreshStartPercent(fast, 1, 60, true, s, 200) == 300);   // 150 x 400 = 600, capped at 600 / 2
    assert(FreshStartPercent(fast, 1, 60, true, s, 500) == 150);   // the cap never goes under the pace
    CatchUpSettings noCap = s;
    noCap.maxTotalPercent = 0;
    assert(FreshStartPercent(fast, 1, 60, true, noCap, 200) == 600);

    // Percentiles of a histogram by level.
    std::vector<uint32_t> h(61, 0);
    h[10] = 50;
    h[20] = 40;
    h[60] = 10;
    assert(LevelAtPercentile(h, 10) == 10 && LevelAtPercentile(h, 50) == 10 && LevelAtPercentile(h, 51) == 20);
    assert(LevelAtPercentile(h, 90) == 20 && LevelAtPercentile(h, 100) == 60);
    assert(LevelAtPercentile(std::vector<uint32_t>(61, 0), 50) == 0);
    return 0;
}
