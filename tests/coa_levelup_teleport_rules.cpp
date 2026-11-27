// Unit check for CoaLevelupTeleportRules.h (AiPlayerbot.CoaLive.LevelupTeleportGuard).
#include "CoaLevelupTeleportRules.h"
#include <cassert>

int main()
{
    // Level up: under level 6 (hubs = the races' starting points), no teleport; from 6, as before.
    assert(!CoaLevelupTeleports(true, 2));
    assert(!CoaLevelupTeleports(true, 5));
    assert(CoaLevelupTeleports(true, 6));
    assert(CoaLevelupTeleports(true, 60));
    // Guard off: as in 3.2.2.
    assert(CoaLevelupTeleports(false, 2));
    // Arrival: a real player within 150 yards refuses it.
    assert(CoaTeleportArrivalSeen(true, 0.0f));
    assert(CoaTeleportArrivalSeen(true, 149.0f * 149.0f));
    assert(!CoaTeleportArrivalSeen(true, 150.0f * 150.0f));
    assert(!CoaTeleportArrivalSeen(true, 400.0f * 400.0f));
    assert(!CoaTeleportArrivalSeen(false, 0.0f));
    return 0;
}
