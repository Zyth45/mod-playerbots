#include "CoaLootRules.h"

#include <cassert>

int main()
{
    // A worldforged pickup just emptied by the player, back to GO_READY: rolled again for the alt, so opened.
    assert(!CoaHeldLootIsWhatOpenerGets(true, 1344800));
    // Still open (GO_ACTIVATED): what it holds is what is left to take.
    assert(CoaHeldLootIsWhatOpenerGets(false, 1344800));
    // No loot table: nothing is rolled again.
    assert(CoaHeldLootIsWhatOpenerGets(true, 0));
    return 0;
}
