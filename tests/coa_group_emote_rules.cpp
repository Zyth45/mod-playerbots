#include "CoaGroupEmoteRules.h"

#include <cassert>

int main()
{
    using namespace CoaGroupEmote;

    // Defaults: one per bot, three per group, in 10 minutes.
    Cap cap;
    assert(cap.Take(1, 100, 1000, 1, 3));
    assert(!cap.Take(1, 100, 1001, 1, 3));          // the same bot again
    assert(cap.Take(2, 100, 1002, 1, 3));
    assert(cap.Take(3, 100, 1003, 1, 3));
    assert(!cap.Take(4, 100, 1004, 1, 3));          // the group has had its three
    assert(cap.Take(4, 200, 1004, 1, 3));           // another group is apart
    assert(cap.Take(1, 100, 1000 + WINDOW_S, 1, 3));  // the first one left the window
    assert(!cap.Take(2, 100, 1000 + WINDOW_S, 1, 3)); // bot 2 still within it

    // 0: no cap.
    Cap open;
    for (uint64_t t = 0; t < 50; ++t)
        assert(open.Take(7, 300, t, 0, 0));
    Cap groupOnly;
    assert(groupOnly.Take(7, 300, 0, 0, 2) && groupOnly.Take(7, 300, 1, 0, 2) && !groupOnly.Take(7, 300, 2, 0, 2));
    return 0;
}
