#include "CoaWorldDungeonSimRules.h"

#include <cassert>

using namespace CoaWorldDungeonSimRules;

int main()
{
    // 0: proportional, one a side for every 250 bots online (the user's scale: 500 = 2, 1 000 = 4, 2 000 = 8).
    assert(GroupsPerFaction(0, 500) == 2);
    assert(GroupsPerFaction(0, 1000) == 4);
    assert(GroupsPerFaction(0, 2000) == 8);
    assert(GroupsPerFaction(0, 0) == 1 && GroupsPerFaction(0, 249) == 1 && GroupsPerFaction(0, 749) == 2);
    assert(GroupsPerFaction(0, 100000) == MAX_GROUPS);
    // An edited value stays as it is (at most 10).
    assert(GroupsPerFaction(2, 2000) == 2 && GroupsPerFaction(3, 0) == 3 && GroupsPerFaction(50, 0) == 10);
    return 0;
}
