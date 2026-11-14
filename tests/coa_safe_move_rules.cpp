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
    return 0;
}
