#include "CoaMainTankRules.h"

#include <cassert>

int main()
{
    using H = CoaPlayerHold;
    // No player main tank: as before. On the bot: aggro. On another tank: held, except for the group's main tank.
    assert(CoaHasAggro(true, H::None, false, false));
    assert(CoaHasAggro(false, H::None, true, false));
    assert(!CoaHasAggro(false, H::None, true, true));
    assert(!CoaHasAggro(false, H::None, false, false));
    assert(CoaHeldByOtherTank(true, H::None, true, false));
    assert(!CoaHeldByOtherTank(true, H::None, true, true));
    assert(!CoaHeldByOtherTank(true, H::None, false, false));
    assert(CoaLooseOnMember(H::None, false));
    assert(!CoaLooseOnMember(H::None, true));

    // Point 2: the player holds it even from a bot carrying the group's main tank flag.
    assert(CoaHasAggro(false, H::Holds, true, true));
    assert(CoaHeldByOtherTank(true, H::Holds, true, true));
    // Point 3: under the rescue health, the bots take it, even with the player in a tank spec.
    assert(!CoaHasAggro(false, H::Rescue, true, false));
    assert(!CoaHeldByOtherTank(true, H::Rescue, true, false));
    assert(CoaLooseOnMember(H::Rescue, true));
    // Point 7: a player main tank out of a tank spec is a tank all the same.
    assert(CoaHasAggro(false, H::Holds, false, false));
    assert(CoaHeldByOtherTank(true, H::Holds, false, false));
    assert(!CoaLooseOnMember(H::Holds, false));
}
