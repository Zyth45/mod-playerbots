#include "CoaHealRequestRules.h"

#include <cassert>

int main()
{
    using namespace CoaHealRequest;

    // Requests, in English and French.
    for (char const* ask : {"heal me", "HEAL ME PLZ", "heal plz", "heal pls!", "need heal", "need a heal", "need heals",
                            "need some heals", "heals?", "heal", "heal me i'm dying", "soigne moi", "soigne-moi",
                            "soignez-moi svp", "soin stp", "besoin de soin", "besoin d'un heal", "heal stp"})
        assert(IsHealRequest(ask));
    // Not requests: talk about heals, a long line, nothing.
    for (char const* other : {"nice heal", "can you heal the tank", "healer?", "the healer is oom", "", "hello",
                              "heal me if you want but this line is far too long to be a call"})
        assert(!IsHealRequest(other));

    // Below full health only.
    assert(Wanted(500, 1000) && !Wanted(1000, 1000) && !Wanted(0, 1000));

    // One heal per player in 15 s, a request answered within 4 s or dropped.
    assert(CooledDown(0, 5) && !CooledDown(1000, 1000 + PLAYER_CD_MS - 1) && CooledDown(1000, 1000 + PLAYER_CD_MS));
    assert(Fresh(1000, 1000 + FRESH_MS - 1) && !Fresh(1000, 1000 + FRESH_MS));

    // The heal picked: fast direct, then slow direct, then over time; the highest rank.
    assert(PickHeal({}) == 0);
    assert(PickHeal({{1, 3000, 40, true}, {2, 1500, 20, true}, {3, 0, 60, false}}) == 2);
    assert(PickHeal({{1, 1500, 20, true}, {4, 1500, 38, true}}) == 4);
    assert(PickHeal({{1, 3000, 40, true}, {3, 0, 60, false}}) == 1);
    assert(PickHeal({{3, 0, 60, false}}) == 3);
    return 0;
}
