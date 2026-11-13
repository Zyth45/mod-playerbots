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

    // The world channel: ten players saying hello in the same second get one answered hello, not ten.
    uint64_t channelLast = 0;
    uint32_t answered = 0;
    for (int player = 0; player < 10; ++player)
        if (ChannelMayGreet(channelLast, 1000))
        {
            channelLast = 1000;
            ++answered;
        }
    assert(answered == 1);
    assert(!ChannelMayGreet(1000, 1000 + CHANNEL_GAP_S - 1) && ChannelMayGreet(1000, 1000 + CHANNEL_GAP_S));

    // Always two bots, fewer only when fewer are there.
    assert(Answers(10) == 2 && Answers(2) == 2 && Answers(1) == 1 && Answers(0) == 0);

    // The first within 3 to 6 s, the next 3 to 10 s after it.
    for (uint32_t roll : {0u, 1u, 3000u, 3001u, 7000u, 7001u, 4294967295u})
    {
        uint64_t const first = FirstDueMs(1000000, roll);
        assert(first >= 1003000 && first <= 1006000);
        uint64_t const second = NextDueMs(first, roll);
        assert(second >= first + 3000 && second <= first + 10000);
    }
    return 0;
}
