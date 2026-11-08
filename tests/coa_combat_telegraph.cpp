// The telegraph table and rule (src/Ai/Coa/Combat/Telegraph/CoaTelegraphRule.h).
#include "Ai/Coa/Combat/Telegraph/CoaTelegraphRule.h"

#include <cassert>

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
    return 0;
}
