// CoA 3.1.7: the night bench of the bots' odd moves (CoaMoveAuditRules.h).
#include "CoaMoveAuditRules.h"

#include <cassert>

using namespace CoaMoveAuditRules;

int main()
{
    // In the air: 2.8 yards above the floor under it (Valorenz, 19/11), never when flying or falling.
    assert(InAir(97.15f, 94.33f, false));
    assert(!InAir(97.15f, 94.33f, true));
    assert(!InAir(99.57f, 99.57f, false));       // on the bank's floor (19/11): not in the air
    assert(!InAir(99.0f, -200000.0f, false));    // no floor found: no line
    // On a roof: outdoors, standing on a model floor well above the terrain.
    assert(OnRoof(true, 101.0f, 96.0f));
    assert(!OnRoof(false, 99.5f, 97.5f));        // indoors on a building's floor (the bank): no
    assert(!OnRoof(true, 97.0f, 96.0f));
    // Too fast, teleport.
    assert(TooFast(10.0f, 300, 7.0f));           // 33 yards a second for a runner at 7
    assert(!TooFast(2.1f, 300, 7.0f));
    assert(!TooFast(14.0f, 1000, 14.0f));        // mounted at its speed
    assert(Teleported(80.0f) && !TooFast(80.0f, 300, 7.0f));
    // Wall check and jumps.
    assert(CheckWall(2.0f) && !CheckWall(0.2f) && !CheckWall(80.0f));
    assert(HeightJump(2.4f, 1.0f, true) && !HeightJump(2.4f, 1.0f, false) && !HeightJump(2.4f, 6.0f, true));
    // Throttle.
    assert(Due(0, 5000, 10000) && !Due(5000, 9000, 10000) && Due(5000, 15000, 10000));
    // Texts.
    assert(PathTypeText(0x01 | 0x04) == "NORMAL|INCOMPLETE" && PathTypeText(0) == "BLANK");
    assert(Json("a\"b\\c") == "\"a\\\"b\\\\c\"");
    // 3.2: back and forth.
    assert(PingPong(300.0f, 30.0f));    // 300 yards walked, never 30 from the start
    assert(!PingPong(300.0f, 120.0f));  // it went somewhere
    assert(!PingPong(100.0f, 30.0f));   // not enough walked
    assert(!PingPong(300.0f, 3.0f));    // turning on the spot
    return 0;
}
