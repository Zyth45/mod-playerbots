// The combat purge's table and clock (src/Ai/Coa/Combat/CombatPurge/CoaCombatPurgeRule.h).
#include "Ai/Coa/Combat/CombatPurge/CoaCombatPurgeRule.h"

#include <cassert>
#include <cstdint>

using namespace CoaCombatPurgeRule;

int main()
{
    // The two CoA rows, always; nothing else.
    assert(IsPurgeable(33, 36530, false));
    assert(IsPurgeable(33, 36530, true));
    assert(IsPurgeable(429, 13916, false));
    assert(!IsPurgeable(33, 13916, false));
    assert(!IsPurgeable(429, 36530, false));
    assert(!IsPurgeable(604, 29982, false));  // Gundrak is out of scope
    assert(HasRowsFor(33) && HasRowsFor(429));
    assert(!HasRowsFor(578) && !HasRowsFor(0));

    // 60 s held, in 1 s sweeps: fires on the 60th and restarts.
    std::uint32_t const window = WindowMs(-1);
    assert(window == 60000);
    std::uint32_t held = 0;
    for (int s = 1; s < 60; ++s)
        assert(!Step(held, 1000, true, window));
    assert(Step(held, 1000, true, window));
    assert(held == 0);

    // One free second restarts the count.
    for (int s = 0; s < 50; ++s)
        Step(held, 1000, true, window);
    assert(!Step(held, 1000, false, window));
    assert(held == 0);

    // Window 0: off.
    held = 0;
    for (int s = 0; s < 100; ++s)
        assert(!Step(held, 1000, true, 0));

    // The old key wins, clamped.
    assert(WindowMs(0) == 0);
    assert(WindowMs(30) == 30000);
    assert(WindowMs(99999) == MAX_WINDOW_SECONDS * 1000u);
    return 0;
}
