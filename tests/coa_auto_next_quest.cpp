#include "CoaAutoNextQuest.h"

#include <cassert>

// Rite of Strength (757) turned in to Chief Hawkwind: Rites of the Earthmother (763, auto-accept) comes at once,
// as for a player; without it, Rite of Vision (767, needs 763) could never be taken.
int main()
{
    assert(CoaTakesAutoNextQuest(true, true, true, true, true));      // 757 -> 763
    assert(!CoaTakesAutoNextQuest(true, true, false, true, true));    // an ordinary next quest: offered, not taken
    assert(!CoaTakesAutoNextQuest(true, false, true, true, true));    // no next quest
    assert(!CoaTakesAutoNextQuest(true, true, true, false, true));    // the bot can't take it (level, class...)
    assert(!CoaTakesAutoNextQuest(true, true, true, true, false));    // log or bags full: the core closes too
    assert(!CoaTakesAutoNextQuest(false, true, true, true, true));    // a game object or an item: the core does not
    return 0;
}
