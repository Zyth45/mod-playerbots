// Unit check for CoaStrandedRules.h (AiPlayerbot.CoaLive.StrandedMinutes).
#include "CoaStrandedRules.h"
#include <cassert>

int main()
{
    using namespace CoaStrandedRules;
    uint32_t const min = 60000;
    // The report: in a group of bots, its player gone, still for 1h18m: out of the group.
    assert(Stranded(78 * min, 10, true, false, true, false) == Release::LeaveGroup);
    // Under the limit, or off: nothing.
    assert(Stranded(9 * min, 10, true, false, true, false) == Release::None);
    assert(Stranded(78 * min, 0, true, false, true, false) == Release::None);
    // A real player in its group: he decides (a bot told to stay).
    assert(Stranded(78 * min, 10, true, true, true, false) == Release::None);
    // Out of a group, idle or resting, nothing wanted: its RPG picks its next goal, nothing more.
    assert(Stranded(10 * min, 10, false, false, true, false) == Release::NextRpg);
    // Out of a group on a goal (a vendor, a trip): left alone.
    assert(Stranded(78 * min, 10, false, false, false, false) == Release::None);
    // The city scene survives: a bot sitting on a square, resting at an inn, away, fishing or crafting, in a capital,
    // still for an hour: left alone.
    assert(Stranded(60 * min, 10, false, false, true, true) == Release::None);
    // Moved: 3 yards.
    assert(!Moved(2.0f, 2.0f, 0.0f));
    assert(Moved(3.0f, 1.0f, 0.0f));
    return 0;
}
