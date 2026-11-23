#include "CoaSafeMoveRules.h"

#include <cassert>
#include <string>

int main()
{
    using namespace CoaSafeMove;

    // A whole path on the mesh; the bot a little off it; a long path cut short.
    assert(Walkable(NORMAL, false) && Walkable(NORMAL | FARFROMPOLY_START, false) && Walkable(NORMAL | SHORT, false));
    // The point off the mesh (in a building), a hole, a partial path, no path: no move.
    assert(!Walkable(NORMAL | FARFROMPOLY_END, false) && !Walkable(NORMAL | SHORTCUT, false));
    assert(!Walkable(INCOMPLETE, false) && !Walkable(NORMAL | INCOMPLETE, false) && !Walkable(NOPATH, false));
    assert(!Walkable(0, false) && !Walkable(SHORTCUT, true));
    // A straight line: only a swim toward a shore in sight.
    assert(!Walkable(NORMAL | NOT_USING_PATH, false) && Walkable(NORMAL | NOT_USING_PATH, true));
    assert(!Walkable(NOPATH | NOT_USING_PATH, true));
    // 3.1.6 trial: a random bot of its own on a normal move in a capital goes only where a whole path leads.
    // Only the core's straight line is refused: a long path cut at the point limit (INCOMPLETE) is followed. Refusing
    // it trapped bots in the Stormwind bank (lot 4 trial).
    assert(CoreStraightLine(NOPATH, 0) && CoreStraightLine(NOPATH | INCOMPLETE, 40));
    assert(!CoreStraightLine(NORMAL | INCOMPLETE, 74));                 // long, cut: followed
    assert(!CoreStraightLine(NORMAL | FARFROMPOLY_END, 12));
    assert(!CoreStraightLine(NORMAL | FARFROMPOLY_START, 12));
    assert(!CoreStraightLine(NORMAL, 2));                               // a short straight walk on the mesh
    assert(CoreStraightLine(NORMAL | INCOMPLETE, 2));                   // two points, cut: the line through the wall
    assert(CoreStraightLine(SHORTCUT, 2) && CoreStraightLine(NORMAL | NOT_USING_PATH, 2));
    // 3.1.8: a still bot off a taxi falls to its floor (Morgan's Vigil: 188.07 over 185.18), only in that case.
    assert(DropsToFloor(188.07f, 185.18f, true, false));
    assert(!DropsToFloor(188.07f, 185.18f, false, false));   // indoors (a floor of a building): left alone
    assert(!DropsToFloor(188.07f, 185.18f, true, true));     // water under it
    assert(!DropsToFloor(188.07f, -200000.0f, true, false)); // no floor found
    assert(!DropsToFloor(185.9f, 185.18f, true, false));     // on its floor (a platform, a tower: its own floor)
    assert(!DropsToFloor(204.0f, 185.18f, true, false));     // too high to be that case: left alone
    // 3.1.8: a teleport to a banker arrives outdoors around the bank, on ground near the banker's floor.
    assert(ArrivalOk(98.0f, 99.7f, true, false, 97.9f));
    assert(!ArrivalOk(98.0f, 99.7f, false, false, 97.9f));   // inside a building
    assert(!ArrivalOk(98.0f, 99.7f, true, true, 97.9f));     // in water (the canals)
    assert(!ArrivalOk(110.0f, 99.7f, true, false, 97.9f));   // a roof or a terrace far above
    assert(!ArrivalOk(-200000.0f, 99.7f, true, false, 97.9f));
    assert(!ArrivalOk(103.6f, 99.7f, true, false, 98.9f));  // the roof by the bank door (bench of 20/11)
    // 3.2: the ledge by the Stormwind bank, a short cut path (20 points) ending 28 yards from the NPC: a dead end.
    assert(DeadEndPath(NORMAL | INCOMPLETE, 20, 28.0f));
    assert(!DeadEndPath(NORMAL | INCOMPLETE, 74, 28.0f));  // cut by the point limit: goes on from its end
    assert(!DeadEndPath(NORMAL | INCOMPLETE, 20, 2.0f));   // ends by the target
    assert(!DeadEndPath(NORMAL, 20, 28.0f));               // a whole path
    assert(CityPathChecked(true, false, false, true, true, true));
    assert(!CityPathChecked(false, false, false, true, true, true));  // a player's alt: left alone
    assert(!CityPathChecked(true, true, false, true, true, true));    // in a real player's group
    assert(!CityPathChecked(true, false, true, true, true, true));    // in a fight
    assert(!CityPathChecked(true, false, false, false, true, true));  // flying or swimming
    assert(!CityPathChecked(true, false, false, true, false, true));  // out of a capital
    assert(!CityPathChecked(true, false, false, true, true, false));  // a forced move (flee)
    // The path types of the trial: no path or a cut path is never walked (the straight line through the wall).
    assert(!Walkable(NOPATH, false) && !Walkable(NORMAL | INCOMPLETE, false) && !Walkable(SHORTCUT, false));
    // 3.2: a stop by an NPC behind its counter.
    assert(StandsByNpc(3.0f));
    assert(StandsByNpc(4.5f));
    assert(!StandsByNpc(6.0f));
    assert(WANDER_GIVE_UPS >= 2);
    // 3.2: a refused move is not a refused way.
    assert(GivesUpNpc(true, true, 0) == GiveUp::Gone);
    assert(GivesUpNpc(false, false, 1) == GiveUp::NoWay);
    assert(GivesUpNpc(false, true, 1) == GiveUp::No);
    assert(GivesUpNpc(false, true, WANDER_REFUSALS) == GiveUp::Refusals);
    assert(std::string(NoWayReason(INCOMPLETE | FARFROMPOLY_START, 10, false, true)) == "bot off the mesh");
    assert(std::string(NoWayReason(INCOMPLETE, 10, false, true)) == "dead end");
    assert(std::string(NoWayReason(NORMAL, 10, true, false)) == "out of sight");
    // 3.2, the living bench's cases. Undercity: 46.2 yards to Auctioneer Yarly over the flat ring (1.4 yards lower).
    assert(FlatLineArrives(46.2f, 1.4f, true));
    assert(!FlatLineArrives(46.2f, 1.4f, false));   // behind a wall
    assert(!FlatLineArrives(46.2f, 4.0f, true));    // over a canal or a ledge
    assert(!FlatLineArrives(80.0f, 0.0f, true));    // too far for a line
    // Stormwind (the path to Chilton stops 16.1 yards short): the bridge rule, kept for 3.2.1 and unused in 3.2.
    assert(BridgesDeadEnd(16.1f, true));
    assert(!BridgesDeadEnd(16.1f, false));           // a wall or a drop in between
    assert(!BridgesDeadEnd(25.0f, true));            // too far short
    assert(!StandsByNpc(16.1f));                     // what refused it before
    // 3.2.1: Chilton's island lies 0.1 yard under the floor; the lifts pass it and stay under a head.
    assert(NpcLift(1) > 0.1f);
    assert(NpcLift(NPC_LIFT_STEPS) < 1.0f);
    return 0;
}
