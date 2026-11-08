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
    // At contact: 3.5 m up or down, as before.
    assert(LootHeightReachable(3.5f, 0.0f));
    assert(LootHeightReachable(-3.5f, 1.0f));
    assert(!LootHeightReachable(3.6f, 1.0f));
    assert(!LootHeightReachable(-4.0f, 2.0f));
    // Ore 12 m up a hillside 20 m off: reachable by walking up.
    assert(LootHeightReachable(12.0f, 20.0f));
    assert(LootHeightReachable(-12.0f, 20.0f));
    // Steeper than 45 degrees (a cliff or a ledge above): still refused.
    assert(!LootHeightReachable(15.0f, 10.0f));
    assert(!LootHeightReachable(-15.0f, 10.0f));
    return 0;
}
