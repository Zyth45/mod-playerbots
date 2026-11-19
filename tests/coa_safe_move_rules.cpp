#include "CoaSafeMoveRules.h"

#include <cassert>

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
    assert(CityPathChecked(true, false, false, true, true, true));
    assert(!CityPathChecked(false, false, false, true, true, true));  // a player's alt: left alone
    assert(!CityPathChecked(true, true, false, true, true, true));    // in a real player's group
    assert(!CityPathChecked(true, false, true, true, true, true));    // in a fight
    assert(!CityPathChecked(true, false, false, false, true, true));  // flying or swimming
    assert(!CityPathChecked(true, false, false, true, false, true));  // out of a capital
    assert(!CityPathChecked(true, false, false, true, true, false));  // a forced move (flee)
    // The path types of the trial: no path or a cut path is never walked (the straight line through the wall).
    assert(!Walkable(NOPATH, false) && !Walkable(NORMAL | INCOMPLETE, false) && !Walkable(SHORTCUT, false));
    return 0;
}
