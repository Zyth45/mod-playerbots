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

    // Main tank flag (07/11, test humain : Pornuus, Sun Cleric gone damage, kept the flag; Pithen, the tank, had none).
    using S = CoaMainTankSeat;
    // A bot holder still tank keeps it.
    assert(CoaMainTankFlagHolder({ { true, true, true, true }, { true, true, true, false } }, false) == 0);
    // A bot holder no longer tank: it moves to the first living tank.
    assert(CoaMainTankFlagHolder({ { true, false, true, true }, { true, true, false, false }, { true, true, true, false } },
                                 false) == 2);
    // No living tank: the first tank, dead or not.
    assert(CoaMainTankFlagHolder({ { true, false, true, true }, { true, true, false, false } }, false) == 1);
    // No tank left: nobody carries it.
    assert(CoaMainTankFlagHolder({ { true, false, true, true }, { true, false, true, false } }, false) == -1);
    // A player's flag stays, whatever the player plays.
    assert(CoaMainTankFlagHolder({ { false, false, true, true }, { true, true, true, false } }, false) == 0);
    // No flag: left alone, unless the raid builder asks for one.
    assert(CoaMainTankFlagHolder({ { true, false, true, false }, { true, true, true, false } }, false) == -1);
    assert(CoaMainTankFlagHolder({ { true, false, true, false }, { true, true, true, false } }, true) == 1);
    assert(CoaMainTankFlagHolder({}, true) == -1);
    (void)sizeof(S);

    // Given target's end (RC11): dead, gone (despawned, other map, evade, not attackable), or kept.
    assert(CoaPinnedTargetEnd(true, true, true, false, true) == CoaPinEnd::Keep);
    assert(CoaPinnedTargetEnd(true, false, true, false, false) == CoaPinEnd::Dead);
    assert(CoaPinnedTargetEnd(false, false, false, false, false) == CoaPinEnd::Gone);
    assert(CoaPinnedTargetEnd(true, true, false, false, true) == CoaPinEnd::Gone);
    assert(CoaPinnedTargetEnd(true, true, true, true, true) == CoaPinEnd::Gone);
    assert(CoaPinnedTargetEnd(true, true, true, false, false) == CoaPinEnd::Gone);
    // Placement in combat: walk while farther than 3 yards; at the point (or out of combat, or no placement), not.
    assert(CoaPlacementMustWalk(true, true, 3.5f));
    assert(!CoaPlacementMustWalk(true, true, 3.0f));
    assert(!CoaPlacementMustWalk(true, false, 20.0f));
    assert(!CoaPlacementMustWalk(false, true, 20.0f));
    // Taunt on the way or at the point: only when the target hits someone else and a ready taunt reaches it.
    assert(CoaPlacementTaunt(false, true, true));
    assert(!CoaPlacementTaunt(true, true, true));
    assert(!CoaPlacementTaunt(false, false, true));
    assert(!CoaPlacementTaunt(false, true, false));

    // "tank main on" during a Dungeon Clear run (active or paused): refused; "off" always taken.
    assert(CoaMainTankOnRefused(true, true));
    assert(!CoaMainTankOnRefused(true, false));
    assert(!CoaMainTankOnRefused(false, true));
}
