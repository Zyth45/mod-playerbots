// When the mount check may run (src/Ai/Coa/Combat/Mount/CoaMountRule.h).
#include "Ai/Coa/Combat/Mount/CoaMountRule.h"

#include <cassert>
#include <cstdint>
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
    // 3.2.2: Novice Riding under 20, Apprentice from 20 (CoA's trainer); a stable mount of its own per bot.
    assert(CoaMountRule::NoviceOrApprentice(5) == CoaMountRule::NOVICE_RIDING_SPELL);
    assert(CoaMountRule::NoviceOrApprentice(19) == CoaMountRule::NOVICE_RIDING_SPELL);
    assert(CoaMountRule::NOVICE_RIDING_SPELL == 33379 && CoaMountRule::NOVICE_RIDING_TEACHER == 33378);  // the riding, not its teacher
    assert(CoaMountRule::NoviceOrApprentice(20) == CoaMountRule::APPRENTICE_RIDING_SPELL);
    assert(CoaMountRule::StableMountIndex(12345, 4) == CoaMountRule::StableMountIndex(12345, 4));
    assert(CoaMountRule::StableMountIndex(12345, 4) < 4 && CoaMountRule::StableMountIndex(7, 0) == 0);
    {
        uint32_t seen[4] = {0, 0, 0, 0};
        for (uint64_t g = 1; g <= 400; ++g)
            ++seen[CoaMountRule::StableMountIndex(g, 4)];
        assert(seen[0] > 50 && seen[1] > 50 && seen[2] > 50 && seen[3] > 50);  // spread over the mounts
    }
    return 0;
}
