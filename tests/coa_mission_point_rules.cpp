#include "CoaMissionPointRules.h"

#include <cassert>

int main()
{
    using namespace CoaMissionPoint;

    // Walking closer: never stuck.
    Progress walk;
    assert(!Stuck(walk, 100.0f, 1000));
    assert(!Stuck(walk, 90.0f, 30000));
    assert(!Stuck(walk, 80.0f, 70000));
    assert(!Stuck(walk, 70.0f, 120000));

    // Standing still (no path into the cave): stuck after a minute, not before.
    Progress still;
    assert(!Stuck(still, 50.0f, 1000));
    assert(!Stuck(still, 50.0f, 1000 + STUCK_MS - 1));
    assert(Stuck(still, 50.0f, 1000 + STUCK_MS));

    // Going back and forth without getting 5 yards closer: stuck too.
    Progress dither;
    assert(!Stuck(dither, 50.0f, 1000));
    assert(!Stuck(dither, 47.0f, 20000));
    assert(!Stuck(dither, 52.0f, 40000));
    assert(Stuck(dither, 46.0f, 1000 + STUCK_MS));

    // A 5 yard gain restarts the minute.
    Progress slow;
    assert(!Stuck(slow, 50.0f, 1000));
    assert(!Stuck(slow, 45.0f, 50000));
    assert(!Stuck(slow, 45.0f, 1000 + STUCK_MS));
    assert(Stuck(slow, 45.0f, 50000 + STUCK_MS));

    // A new point (reset) starts again.
    Progress reset;
    reset = Progress();
    assert(!Stuck(reset, 10.0f, 0));
    return 0;
}
