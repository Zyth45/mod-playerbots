#include "CoaImmersionRules.h"

#include <cassert>
#include <random>

using namespace CoaImmersion;

int main()
{
    BotView b;
    b.knowsFishing = true;
    assert(MayStartFishing(b));
    BotView x = b;
    x.knowsFishing = false;
    assert(!MayStartFishing(x));
    x = b;
    x.ownedByPlayer = true;
    assert(!MayStartFishing(x));
    x = b;
    x.inCombat = true;
    assert(!MayStartFishing(x));
    x = b;
    x.inInstanceOrCity = true;
    assert(!MayStartFishing(x));
    x = b;
    x.idleStatus = false;
    assert(!MayStartFishing(x));
    x = b;
    x.randomBot = false;
    assert(!MayStartFishing(x));

    std::mt19937 gen(5);
    auto between = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(gen); };
    for (int i = 0; i < 100; ++i)
    {
        uint32_t const ms = FishingMs(between);
        assert(ms >= 240000 && ms <= 900000);
    }

    // The cap: 25 per thousand of the bots online by default.
    assert(FishingRoom(24, 1000, 25) && !FishingRoom(25, 1000, 25) && !FishingRoom(40, 1000, 25));
    assert(FishingRoom(0, 10, 25) && !FishingRoom(1, 10, 25));  // at least one, at most one for 10 bots
    assert(!FishingRoom(0, 1000, 0) && !FishingRoom(0, 0, 25));

    assert(!MustStop(1000, 2000, false, false, true));
    assert(MustStop(2000, 2000, false, false, true));
    assert(MustStop(1000, 2000, true, false, true));
    assert(MustStop(1000, 2000, false, true, true));
    assert(MustStop(1000, 2000, false, false, false));
    return 0;
}
