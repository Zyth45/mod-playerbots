#include "CoaPlayerReplyRules.h"

#include <cassert>

int main()
{
    using namespace CoaPlayerReply;

    // An emote back for a wave, a hello, a bow, a thank-you, a cheer, a hug; nothing for the rest.
    assert(EmoteBack(101) == 101 && EmoteBack(55) == 101 && EmoteBack(48) == 101);
    assert(EmoteBack(17) == 17 && EmoteBack(78) == 17 && EmoteBack(97) == 17);
    assert(EmoteBack(21) == 21 && EmoteBack(56) == 56);
    assert(EmoteBack(0) == 0 && EmoteBack(34) == 0);

    // Two bots, one for the single replies, never more than are there.
    assert(Speakers(5, false) == 2 && Speakers(1, false) == 1 && Speakers(0, false) == 0);
    assert(Speakers(5, true) == 1 && Speakers(0, true) == 0);

    // Cooldowns.
    assert(CooledDown(0, 10, INSULT_CD_S));
    assert(!CooledDown(100, 100 + INSULT_CD_S - 1, INSULT_CD_S) && CooledDown(100, 100 + INSULT_CD_S, INSULT_CD_S));

    // A deadly fall, a fine drop, a fine mount.
    assert(DeadlyFall(500, 500) && DeadlyFall(900, 500) && !DeadlyFall(100, 500) && !DeadlyFall(100, 0));
    assert(FineDrop(4, 70) && FineDrop(5, 80) && FineDrop(3, 40) && !FineDrop(3, 60) && !FineDrop(2, 20));
    assert(FineMount(true, 2.0f) && !FineMount(true, 1.6f) && !FineMount(false, 2.0f));

    // Insults, in English and French; not a reproach of play, not a joke about bots, not a long line.
    for (char const* insult : {"idiot", "you're so stupid", "noob", "shut up", "STFU", "ta gueule", "t'es nul",
                               "espèce d'abruti", "connard", "useless tank"})
        assert(IsInsult(insult));
    for (char const* other : {"bad pull", "lol bot", "are you a bot?", "nice one", "", "hello",
                              "this was a long line where i call nobody an idiot at all really"})
        assert(!IsInsult(other));
    return 0;
}
