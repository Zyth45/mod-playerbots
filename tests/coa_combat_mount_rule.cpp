// When the mount check may run (src/Ai/Coa/Combat/Mount/CoaMountRule.h).
#include "Ai/Coa/Combat/Mount/CoaMountRule.h"

#include <cassert>
#include <cstdio>

using namespace CoaMountRule;

int main()
{
    // At or above the mount level: always.
    assert(CheckAllowed(20, 20, false, false));
    assert(CheckAllowed(60, 20, true, false));

    // Below it, on foot: only to follow a mounted master.
    assert(!CheckAllowed(12, 20, false, false));
    assert(CheckAllowed(12, 20, false, true));

    // The reported bug: a level 12 alt rode with its master; the master got off and attacked first. The alt is
    // still mounted and its master no longer is: the check must still run, so the alt can get off.
    assert(CheckAllowed(12, 20, true, false));
    assert(DismountForMaster(false, true));

    // A mounted master keeps the alt mounted; a bot on foot has nothing to get off.
    assert(!DismountForMaster(true, true));
    assert(!DismountForMaster(false, false));

    std::puts("coa_combat_mount_rule: ok");
    return 0;
}
