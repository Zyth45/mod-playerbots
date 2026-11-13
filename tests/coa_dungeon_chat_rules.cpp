#include "CoaDungeonChatRules.h"

#include <cassert>

int main()
{
    using namespace CoaDungeonChat;

    // Two bots at the entrance and the end, one when the group has one.
    assert(Speakers(4) == 2 && Speakers(2) == 2 && Speakers(1) == 1 && Speakers(0) == 0);

    // Now and then: not in the first 2 minutes, then every 4 to 6 minutes, never in a fight.
    uint64_t const enter = 1000000;
    for (uint32_t roll : {0u, 1u, 120000u, 4294967295u})
    {
        uint64_t const first = FirstIdleMs(enter, roll);
        assert(first >= enter + FIRST_IDLE_MS && first <= enter + FIRST_IDLE_MS + IDLE_JITTER_MS);
        uint64_t const next = NextIdleMs(first, roll);
        assert(next >= first + IDLE_MIN_MS && next <= first + IDLE_MIN_MS + IDLE_JITTER_MS);
    }
    assert(!IdleDue(enter + 10, enter, false));
    assert(IdleDue(enter, enter, false));
    assert(!IdleDue(enter, enter + 1, true));  // a fight: wait

    // In an hour, at most one line per 4 minutes: 15 at the very most.
    uint64_t now = enter, nextIdle = FirstIdleMs(enter, 0);
    uint32_t lines = 0;
    for (; now < enter + 60 * 60 * 1000; now += 30000)
        if (IdleDue(nextIdle, now, false))
        {
            ++lines;
            nextIdle = NextIdleMs(now, 0);
        }
    assert(lines >= 10 && lines <= 15);

    // The end only after 5 minutes in.
    assert(!EndDue(enter, enter + END_AFTER_MS - 1) && EndDue(enter, enter + END_AFTER_MS));

    // A new entrance: another instance, or the same one 30 minutes later; not back after a death.
    assert(NewEntrance(0, 0, 7, enter));
    assert(NewEntrance(6, enter, 7, enter + 1000));
    assert(!NewEntrance(7, enter, 7, enter + 60000));
    assert(NewEntrance(7, enter, 7, enter + REENTER_MS));

    // The two lines 3 to 6 s, then 3 to 10 s apart.
    for (uint32_t roll : {0u, 3000u, 7000u, 4294967295u})
    {
        uint64_t const a = FirstDueMs(enter, roll);
        uint64_t const b = NextDueMs(a, roll);
        assert(a >= enter + 3000 && a <= enter + 6000 && b >= a + 3000 && b <= a + 10000);
    }
    return 0;
}
