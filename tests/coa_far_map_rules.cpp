// Unit check for CoaFarMapRules.h (AiPlayerbot.CoaLive.FarMaps.Allow).
#include "CoaFarMapRules.h"
#include <cassert>

int main()
{
    // Default (0): maps 530 and 571 closed to the random bots, the old continents and the instances open.
    assert(CoaFarMapBlocked(false, 530));
    assert(CoaFarMapBlocked(false, 571));
    assert(!CoaFarMapBlocked(false, 0));
    assert(!CoaFarMapBlocked(false, 1));
    assert(!CoaFarMapBlocked(false, 609));
    assert(!CoaFarMapBlocked(false, 543));  // Hellfire Ramparts: a map of its own
    // Allowed (1): no map is closed.
    assert(!CoaFarMapBlocked(true, 530));
    assert(!CoaFarMapBlocked(true, 571));
    assert(!CoaFarMapBlocked(true, 0));
    return 0;
}
