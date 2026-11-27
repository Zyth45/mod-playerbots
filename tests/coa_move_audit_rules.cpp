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
    // 3.2.2: 80 yards in 22 seconds is a run, not a teleport; 5500 yards in 2 seconds is one.
    assert(!TeleportedFor(80.0f, 22000, 7.0f));
    assert(TeleportedFor(5500.0f, 2000, 7.0f));
    assert(TeleportedFor(80.0f, 300, 7.0f));
    // 3.2.2: through the relief: a hill 5 yards over both ends; a cave or a city under the terrain is not judged.
    assert(ThroughTerrain(100.0f, 100.0f, 100.5f, 100.2f, 105.0f));
    assert(!ThroughTerrain(100.0f, 100.0f, 100.5f, 100.2f, 101.0f));   // a small rise
    assert(!ThroughTerrain(60.0f, 100.0f, 60.0f, 100.0f, 105.0f));     // under the terrain (Undercity)
    // 3.2.2: in a wall: the head sees out in one direction or none.
    assert(InWall(0) && InWall(1) && !InWall(2));
    // 3.2.2: the Stormwind bank: a bot at 97.7 under the floor at 99.6.
    assert(UnderFloor(97.7f, 99.6f));
    assert(!UnderFloor(97.7f, 97.7f));     // its own floor
    assert(!UnderFloor(97.7f, -200000.0f));
    assert(!UnderFloor(1317.65f, 1322.0f));  // 3.2.2: the next storey over Darnassus's bank, not a floor it is under
    // 3.2.2: Ironforge's bank: on the model's floor at 503.94, never under a floor; under Stormwind's bank, no model.
    assert(OnModelFloor(504.17f, 503.94f));
    assert(!OnModelFloor(97.7f, -200000.0f));
    assert(!OnModelFloor(97.7f, 96.0f));
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
