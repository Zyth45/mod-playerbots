#include "CoaWorldBgSimRules.h"

#include <cassert>

using namespace CoaWorldBgSimRules;

int main()
{
    // As the core: 1.55 honor per level per kill, rounded up, level capped at 80.
    assert(KillsHonor(60, 1.0f) == 93);
    assert(KillsHonor(69, 30.0f) == 3209);
    assert(KillsHonor(90, 1.0f) == 124);

    assert(AllianceWinPct(60, 60) == 50);
    assert(AllianceWinPct(62, 60) == 56);
    assert(AllianceWinPct(70, 55) == 70 && AllianceWinPct(40, 60) == 30);

    // A Warsong win at 60-69: 30 end kills, 3 captures (6), 6 HKs shared by 3.
    uint32_t const win = MatchHonor(69, 30, 6, 6, 3);
    assert(win == KillsHonor(69, 36.0f) + 6 * 107 / 3);
    // A loss gives far less.
    assert(MatchHonor(69, 5, 2, 6, 3) < win / 3);
    assert(MatchHonor(69, 5, 0, 0, 0) == KillsHonor(69, 5.0f));

    assert(ArathiTicks(1600) == 6 && ArathiTicks(259) == 0);
    assert(MatchMinutes(12, 25, 0) == 12 && MatchMinutes(12, 25, 100) == 25 && MatchMinutes(12, 25, 200) == 25 && MatchMinutes(5, 5, 50) == 5);
    assert(EnoughPerSide(5, 10) && !EnoughPerSide(4, 10) && EnoughPerSide(8, 15) && !EnoughPerSide(1, 2));
    return 0;
}
