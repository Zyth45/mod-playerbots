// Unit check for CoaOrphanRules.h (AiPlayerbot.CoaOrphanGroupReleaseDelay).
#include "CoaOrphanRules.h"
#include <cassert>

int main()
{
    using namespace CoaOrphanRules;
    // A leveling party of random bots that never held a player: never orphan.
    assert(!Orphan(false, false, false, false));
    // A real player, a selfbot, an alt or addclass bot connected in it: never orphan.
    assert(!Orphan(true, true, true, true));
    // Its player logged out and stays in the raid: orphan, the full delay.
    assert(Orphan(false, true, false, true) && !Left(false, true, false, true));
    // Its player left (/leave), still connected: orphan, 30 seconds.
    assert(Orphan(false, false, true, true) && Left(false, false, true, true));
    // The report: /leave, then a logout (the bots' master cleared): orphan by the mark alone, 30 seconds.
    assert(Orphan(false, false, false, true) && Left(false, false, false, true));
    return 0;
}
