#include "CoaGreetReplyRules.h"
#include "../src/Ai/Coa/World/CoaWorldGuildRules.h"

#include <cassert>

int main()
{
    using namespace CoaGreetReply;

    // A hello, in English and French, short.
    for (char const* hi : {"hi", "hello", "hey all", "yo", "o/", "salut", "bonjour tout le monde", "coucou", "re",
                           "Hi everyone!"})
        assert(CoaWorldGuildRules::IsGreeting(hi));
    // Not a hello: a question, a long line, anything else.
    for (char const* other : {"lf healer", "this is a long line that says hi to nobody", "where is the bank", ""})
        assert(!CoaWorldGuildRules::IsGreeting(other));

    // One answer per player in a minute, then again.
    assert(PlayerMayGreet(0, 100));
    assert(!PlayerMayGreet(100, 100 + PLAYER_CD_S - 1));
    assert(PlayerMayGreet(100, 100 + PLAYER_CD_S));

    // One or two bots, never more than there are, never more than two.
    for (uint32_t roll = 0; roll < 100; ++roll)
    {
        uint32_t const n = Answers(roll, 10);
        assert(n >= 1 && n <= MAX_ANSWERS);
        assert(Answers(roll, 1) == 1);
        assert(Answers(roll, 0) == 0);
    }
    assert(Answers(0, 10) == 1 && Answers(1, 10) == 2);

    // Within 3 to 10 seconds.
    for (uint32_t roll : {0u, 1u, 3000u, 3001u, 4294967295u})
    {
        uint64_t const first = FirstDueMs(1000000, roll);
        assert(first >= 1003000 && first <= 1006000);
        uint64_t const second = SecondDueMs(first, roll);
        assert(second >= first + 2000 && second <= first + 4000 && second <= 1010000);
    }
    return 0;
}
