// The telegraph table and rule (src/Ai/Coa/Combat/Telegraph/CoaTelegraphRule.h).
#include "Ai/Coa/Combat/Telegraph/CoaTelegraphRule.h"

#include <cassert>
#include <cmath>

using namespace CoaTelegraphRule;

int main()
{
    // Targorr's marker and its twin are listed; an ordinary spell is not.
    Telegraph const* targorr = Find(2102735);
    assert(targorr && Find(2100633));
    assert(!Find(0) && !Find(133));

    // 4 yd marker, 1.5 yd margin: safe from 5.5 yd.
    assert(SafeDistance(*targorr, 4.0f) == 5.5f);
    assert(MustLeave(*targorr, 4.0f, 0.0f));
    assert(MustLeave(*targorr, 4.0f, 3.9f));
    assert(MustLeave(*targorr, 4.0f, 5.4f));  // inside the margin: still leaving
    assert(!MustLeave(*targorr, 4.0f, 5.5f));
    assert(!MustLeave(*targorr, 4.0f, 12.0f));

    // A marker with no radius read is never fled.
    assert(!MustLeave(*targorr, 0.0f, 0.0f));
    // Bench a39: the approach moves wait while the bot is within the safe distance plus HOLD_RANGE of a live marker.
    assert(HoldApproach(*targorr, 4.0f, 0.0f));
    assert(HoldApproach(*targorr, 4.0f, 15.4f));
    assert(!HoldApproach(*targorr, 4.0f, 15.5f));
    assert(!HoldApproach(*targorr, 0.0f, 1.0f));
    // The way out is straight from the centre through the bot, to the given distance; on the centre, the fallback.
    float x = 0.0f, y = 0.0f;
    ExitPoint(10.0f, 10.0f, 12.0f, 10.0f, 3.0f, 0.0f, 6.0f, x, y);
    assert(std::fabs(x - 16.0f) < 0.01f && std::fabs(y - 10.0f) < 0.01f);
    ExitPoint(10.0f, 10.0f, 10.0f, 10.0f, 1.5707963f, 0.0f, 6.0f, x, y);
    assert(std::fabs(x - 10.0f) < 0.01f && std::fabs(y - 16.0f) < 0.01f);
    ExitPoint(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.5707963f, 5.0f, x, y);  // a wall: turned a quarter
    assert(std::fabs(x) < 0.01f && std::fabs(y - 5.0f) < 0.01f);

    return 0;
}
