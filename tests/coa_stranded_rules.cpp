// Unit check for CoaStrandedRules.h (AiPlayerbot.CoaLive.StrandedMinutes).
#include "CoaStrandedRules.h"
#include <cassert>

int main()
{
    using namespace CoaStrandedRules;
    uint32_t const min = 60000;
    // The report: in a group of bots, its player gone, still for 1h18m: released.
    assert(Stranded(78 * min, 10, true, false, true));
    // Under the limit, or off: nothing.
    assert(!Stranded(9 * min, 10, true, false, true));
    assert(!Stranded(78 * min, 0, true, false, true));
    // A real player in its group: he decides (a bot told to stay).
    assert(!Stranded(78 * min, 10, true, true, true));
    // Out of a group: only an idle or resting RPG.
    assert(Stranded(10 * min, 10, false, false, true));
    assert(!Stranded(78 * min, 10, false, false, false));
    // Moved: 3 yards.
    assert(!Moved(2.0f, 2.0f, 0.0f));
    assert(Moved(3.0f, 1.0f, 0.0f));
    return 0;
}
