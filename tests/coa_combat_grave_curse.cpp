// Call of the Grave (src/Ai/Coa/Combat/Curse/CoaGraveCurseRule.h).
#include "Ai/Coa/Combat/Curse/CoaGraveCurseRule.h"

#include <cassert>
#include <cstdio>

using namespace CoaGraveCurseRule;

int main()
{
    // The three difficulties of the curse, and nothing else.
    assert(IsGraveCurse(17831) && IsGraveCurse(159900) && IsGraveCurse(387251));
    assert(!IsGraveCurse(387250) && !IsGraveCurse(0));

    // Spectre and Phantasm, base and difficulty copies; not their neighbours.
    assert(IsGraveCaster(11473) && IsGraveCaster(11475) && IsGraveCaster(311475));
    assert(!IsGraveCaster(11474) && !IsGraveCaster(411473));

    // A melee damage dealer within 6.5 yd leaves; a tank or a ranged bot stays; past the margin nothing moves.
    assert(MustLeave(false, true, 4.0f));
    assert(MustLeave(false, true, 6.0f));
    assert(!MustLeave(true, true, 3.0f));
    assert(!MustLeave(false, false, 3.0f));
    assert(!MustLeave(false, true, 7.0f));
    assert(KEEP_YARDS > CURSE_YARDS);

    std::puts("coa_combat_grave_curse: ok");
    return 0;
}
