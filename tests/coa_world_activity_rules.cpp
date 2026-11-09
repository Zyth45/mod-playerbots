#include "CoaWorldActivityRules.h"

#include <cassert>
#include <set>

using namespace CoaWorldActivity;

int main()
{
    // One key per (map, zone): Elwynn on Eastern Kingdoms is not zone 12 of Kalimdor nor map 12.
    std::set<uint64_t> keys{ ZoneKey(0, 12), ZoneKey(1, 12), ZoneKey(12, 0), ZoneKey(0, 1519) };
    assert(keys.size() == 4);

    // Only an asleep bot, out of combat, alone in its zone runs its AI less often.
    assert(MaySlowDown(false, false, false));
    assert(!MaySlowDown(true, false, false));
    assert(!MaySlowDown(false, true, false));
    assert(!MaySlowDown(false, false, true));

    assert(!SlowAIDue(1999, 2000));
    assert(SlowAIDue(2000, 2000));
    assert(SlowAIDue(0, 0));
    return 0;
}
