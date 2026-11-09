#include "CoaWorldAiBudgetRules.h"

#include <cassert>

using CoaWorldAiBudgetRules::Skip;

int main()
{
    assert(!Skip(false, false, false, false, false));  // under the budget: everyone thinks
    assert(Skip(true, false, false, false, false));    // over it: a free bot waits
    assert(!Skip(true, true, false, false, false));    // never two ticks in a row
    assert(!Skip(true, false, true, false, false));    // in combat
    assert(!Skip(true, false, false, true, false));    // grouped with a real player
    assert(!Skip(true, false, false, false, true));    // a real player near
    return 0;
}
