// The stuck combat breaker's rules (src/Ai/Coa/Combat/StuckCombat/CoaStuckCombatRule.h).
#include "Ai/Coa/Combat/StuckCombat/CoaStuckCombatRule.h"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace CoaStuckCombatRule;

int main()
{
    // Phantom only when flagged with nothing at all.
    assert(IsPhantomCombat(true, false, false, false));
    assert(!IsPhantomCombat(false, false, false, false));
    assert(!IsPhantomCombat(true, true, false, false));
    assert(!IsPhantomCombat(true, false, true, false));
    assert(!IsPhantomCombat(true, false, false, true));

    // A legitimate holder prosecutes the fight in engage range or while closing; unseen ones only while closing.
    float const inf = std::numeric_limits<float>::infinity();
    assert(IsHolderProsecutingFight(true, 10.0f, ENGAGE_RANGE, false));
    assert(IsHolderProsecutingFight(true, inf, ENGAGE_RANGE, true));
    assert(!IsHolderProsecutingFight(true, inf, ENGAGE_RANGE, false));
    assert(!IsHolderProsecutingFight(true, 60.0f, ENGAGE_RANGE, false));
    assert(!IsHolderProsecutingFight(false, 0.0f, ENGAGE_RANGE, true));

    // Only a trigger creature is unresolvable.
    assert(IsUnresolvableCombatHolder(true, true));
    assert(!IsUnresolvableCombatHolder(true, false));
    assert(!IsUnresolvableCombatHolder(false, true));

    // The streak: 15 s of continuous phantom combat; a gap restarts it; 0 switches it off.
    std::uint32_t since = 0;
    assert(!ShouldBreakStuckCombat(true, 1000, 15000, since));
    assert(since == 1000);
    assert(!ShouldBreakStuckCombat(true, 15999, 15000, since));
    assert(ShouldBreakStuckCombat(true, 16000, 15000, since));
    assert(!ShouldBreakStuckCombat(false, 17000, 15000, since));
    assert(since == 0);
    assert(!ShouldBreakStuckCombat(true, 18000, 15000, since));
    assert(!ShouldBreakStuckCombat(true, 32000, 15000, since));
    assert(ShouldBreakStuckCombat(true, 33000, 15000, since));
    assert(!ShouldBreakStuckCombat(true, 99000, 0, since));
    assert(since == 0);
    // Arming at ms 0 uses the 1 sentinel; a backward step never fires.
    assert(!ShouldBreakStuckCombat(true, 0, 15000, since));
    assert(since == 1);
    since = 5000;
    assert(!ShouldBreakStuckCombat(true, 4000, 1, since));

    // The closing watch: the first sample closes, then only a 0.5 yd gain does.
    ClosingWatch w;
    assert(w.Tick(60.0f, CLOSING_STEP));
    assert(!w.Tick(60.0f, CLOSING_STEP));
    assert(!w.Tick(59.6f, CLOSING_STEP));
    assert(w.Tick(59.4f, CLOSING_STEP));
    assert(!w.Tick(70.0f, CLOSING_STEP));  // walking away from a stopped holder is no progress
    w.Reset();
    assert(w.Tick(70.0f, CLOSING_STEP));

    // The timeout: the old key first, clamped, 0 or negative off.
    assert(TimeoutMs(-1.0f, 15.0f) == 15000);
    assert(TimeoutMs(30.0f, 15.0f) == 30000);
    assert(TimeoutMs(0.0f, 15.0f) == 0);
    assert(TimeoutMs(-1.0f, 0.0f) == 0);
    assert(TimeoutMs(-1.0f, 500.0f) == 120000);
    return 0;
}
