#include "CoaRefreshRules.h"

#include <cassert>

int main()
{
    // Default (1): the bags of a geared bot are emptied as before; 0 keeps them.
    assert(CoaRefreshClearsInventory(false, true));
    assert(!CoaRefreshClearsInventory(false, false));
    // A self-found bot keeps what it looted whatever the option.
    assert(!CoaRefreshClearsInventory(true, true));
    assert(!CoaRefreshClearsInventory(true, false));
    // Empty bags: the whole target is added.
    assert(CoaRefreshMissingCount(20, 0) == 20);
    // Partly used: topped up only.
    assert(CoaRefreshMissingCount(20, 7) == 13);
    // Already full or more (bought, looted): nothing added, no pile.
    assert(CoaRefreshMissingCount(20, 20) == 0);
    assert(CoaRefreshMissingCount(20, 45) == 0);
    return 0;
}
