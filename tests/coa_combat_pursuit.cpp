// The pursuit stop (src/Ai/Coa/Combat/Pursuit/CoaPursuitRule.h).
#include "Ai/Coa/Combat/Pursuit/CoaPursuitRule.h"

#include <cassert>
#include <cmath>

using namespace CoaPursuitRule;

namespace
{
    bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }
}

int main()
{
    // Chase end geometry. Bot at 0, target at 30 east, tank at 0.
    assert(Near(ChaseEndToTank(0, 0, 30, 0, 0, 0, 0.0f), 30.0f));   // melee: ends on the target
    assert(Near(ChaseEndToTank(0, 0, 30, 0, 0, 0, 25.0f), 5.0f));   // reach spell 25: stops 25 short
    assert(Near(ChaseEndToTank(0, 0, 30, 0, 0, 0, 40.0f), 0.0f));   // already in reach: does not move
    assert(Near(ChaseEndToTank(5, 0, 5, 0, 0, 0, 0.0f), 5.0f));     // on the target already

    Situation s;
    s.chaseAction = true;
    s.targetFleeing = true;
    s.hasTank = true;
    s.chaseEndToTank = 30.0f;
    assert(Forbids(s));  // a DPS running after a fleeing mob 30 yd from the tank

    s.chaseEndToTank = LEASH_YARDS;
    assert(!Forbids(s));  // on the leash: allowed
    s.chaseEndToTank = 30.0f;

    Situation t = s;
    t.botIsTank = true;  // the tank may go for it
    assert(!Forbids(t));
    t = s;
    t.targetFleeing = false;  // an ordinary chase is left alone
    assert(!Forbids(t));
    t = s;
    t.hasTank = false;  // no tank to stay with
    assert(!Forbids(t));
    t = s;
    t.chaseAction = false;  // shooting it from where it stands is fine
    assert(!Forbids(t));
    return 0;
}
