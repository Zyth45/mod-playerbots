#include "CoaWorldLfgLevelRules.h"

#include <cassert>

using namespace CoaWorldLfgLevelRules;

int main()
{
    // A player of 60: 58-62 in the first two minutes, 55-65 after.
    assert(!NearPlayers(57, 60, 60, 0));
    assert(NearPlayers(58, 60, 60, 0));
    assert(NearPlayers(62, 60, 60, 119));
    assert(!NearPlayers(63, 60, 60, 119));
    assert(!NearPlayers(55, 60, 60, 60));
    assert(NearPlayers(55, 60, 60, 120));
    assert(NearPlayers(65, 60, 60, 600));
    assert(!NearPlayers(54, 60, 60, 600));
    assert(!NearPlayers(66, 60, 60, 600));

    // A player of 15: 13-17, then 10-20; nothing wraps below zero.
    assert(!NearPlayers(12, 15, 15, 0));
    assert(NearPlayers(13, 15, 15, 0));
    assert(NearPlayers(17, 15, 15, 0));
    assert(NearPlayers(10, 15, 15, 120));
    assert(!NearPlayers(9, 15, 15, 120));
    assert(NearPlayers(1, 2, 2, 0));

    // Players of 56 and 60 in the same queue: 54-62, then 51-65; the longest wait counts.
    assert(NearPlayers(54, 56, 60, 0));
    assert(!NearPlayers(53, 56, 60, 0));
    assert(NearPlayers(62, 56, 60, 0));
    assert(!NearPlayers(63, 56, 60, 0));
    assert(NearPlayers(51, 56, 60, 130));
    assert(NearPlayers(65, 56, 60, 130));

    assert(Spread(0) == NEAR_SPREAD && Spread(WIDE_AFTER_SECONDS) == WIDE_SPREAD);
    return 0;
}
