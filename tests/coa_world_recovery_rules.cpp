#include "CoaWorldRecoveryRules.h"

#include <cassert>

using namespace CoaWorldRecoveryRules;

int main()
{
    // Darkcloud Pinnacle: bots follow each other into death on the same quest.
    Strikes strikes;
    assert(!Strike(strikes, 101, 1000, 2));       // one bot: only its own quarantine
    assert(!Strike(strikes, 101, 1500, 2));       // the same bot again counts once
    assert(Strike(strikes, 202, 2000, 2));        // a second bot: every bot leaves the quest aside
    assert(strikes.size() == 2);

    // Strikes older than the window are forgotten.
    Strikes old;
    assert(!Strike(old, 1, 1000, 2));
    assert(!Strike(old, 2, 1000 + SHARED_WINDOW_SECONDS + 1, 2));
    assert(old.size() == 1);

    // Three bots needed when set so; 0 never shares.
    Strikes three;
    assert(!Strike(three, 1, 10, 3) && !Strike(three, 2, 20, 3) && Strike(three, 3, 30, 3));
    Strikes off;
    assert(!Strike(off, 1, 10, 0) && !Strike(off, 2, 20, 0));
    return 0;
}
