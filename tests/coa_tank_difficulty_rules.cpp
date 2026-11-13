#include "CoaTankDifficultyRules.h"

#include <cassert>

int main()
{
    using namespace CoaTankDifficulty;

    // Normal dungeon, open world: not hard. Heroic, mythic, any raid: hard.
    assert(!HardContent(false, 0));
    assert(HardContent(false, 1) && HardContent(false, 2));
    assert(HardContent(true, 0) && HardContent(true, 3));

    std::set<uint32_t> const eternal = {99};
    assert(MayTank(eternal, 99, false));   // Eternal in a normal dungeon
    assert(!MayTank(eternal, 99, true));   // not in heroic, mythic or raid
    assert(MayTank(eternal, 26, true));    // another tank specialization goes everywhere
    assert(MayTank({}, 99, true));         // empty key: no restriction
    return 0;
}
