// The off-tank's ranking rule (src/Ai/Coa/Combat/OffTank/CoaOffTank.h).
#include "Ai/Coa/Combat/OffTank/CoaOffTank.h"

#include <cassert>

int main()
{
    bool decided = false;
    // A loose add ranks before a mob another tank holds, whatever the order of comparison.
    assert(CoaOffTank::RanksBefore(false, true, decided) && decided);
    assert(!CoaOffTank::RanksBefore(true, false, decided) && decided);
    // Two of a kind: undecided, the picker's own order rules.
    assert(!CoaOffTank::RanksBefore(false, false, decided) && !decided);
    assert(!CoaOffTank::RanksBefore(true, true, decided) && !decided);
    return 0;
}