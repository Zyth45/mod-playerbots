#include "CoaWorldCharterRules.h"

#include <cassert>

using namespace CoaWorldCharterRules;

int main()
{
    // Only a guild charter, only a random bot of its own without a guild nor an invitation.
    assert(StrangerMaySign(true, true, false, false, false));
    assert(!StrangerMaySign(false, true, false, false, false));  // an arena team charter keeps the rules
    assert(!StrangerMaySign(true, false, false, false, false));  // an alt / a player's bot keeps the rules
    assert(!StrangerMaySign(true, true, true, false, false));    // a random bot with a real master keeps them
    assert(!StrangerMaySign(true, true, false, true, false));
    assert(!StrangerMaySign(true, true, false, false, true));

    assert(UnguildedPct(30, 100) == 30 && UnguildedPct(0, 0) == 100);
    // 1000 bots, 400 without a guild, floor 30%: a charter of 10 goes (39%), not when only 305 are left.
    assert(AboveFloor(400, 1000, 10, 30));
    assert(!AboveFloor(305, 1000, 10, 30));
    assert(AboveFloor(310, 1000, 10, 30));
    assert(AboveFloor(0, 1000, 10, 0));
    assert(!AboveFloor(5, 1000, 10, 1));

    assert(AsksSignatures("can someone sign my charter?"));
    assert(AsksSignatures("Signatures please!"));
    assert(AsksSignatures("qui peut signer ma charte"));
    assert(!AsksSignatures("design is nice"));
    assert(!AsksSignatures("hello there"));
    return 0;
}
