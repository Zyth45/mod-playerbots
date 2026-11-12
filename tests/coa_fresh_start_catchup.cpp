#include "CoaFreshStartRules.h"

#include <cassert>

using namespace CoaFreshStart;

int main()
{
    CatchUpSettings s;
    // The front: median of the active humans' best characters, none under MinHumans.
    assert(MedianFront({}, 2) == 0);
    assert(MedianFront({40}, 2) == 0);
    assert(MedianFront({40}, 1) == 40);
    assert(MedianFront({60, 20}, 2) == 20);  // lower middle: one rushing player does not pull it
    assert(MedianFront({60, 30, 25}, 2) == 30);
    assert(MedianFront({60, 59, 12, 11, 10}, 2) == 12);
    // The default (MinHumans 1): a player alone is the front; two players, the slower one; none, no front.
    assert(s.minHumans == 1);
    assert(MedianFront({}, s.minHumans) == 0);
    assert(MedianFront({23}, s.minHumans) == 23);
    assert(MedianFront({23, 41}, s.minHumans) == 23);
    assert(CatchUpPercent(10, MedianFront({23}, s.minHumans), s) == 350);  // 13 behind, 10 beyond the margin

    // The bonus: none within the margin or ahead, +25 % a level beyond, at most x4.
    assert(CatchUpPercent(10, 0, s) == 100);
    assert(CatchUpPercent(30, 30, s) == 100);
    assert(CatchUpPercent(35, 30, s) == 100);  // ahead: never slowed
    assert(CatchUpPercent(27, 30, s) == 100);  // 3 behind: the margin
    assert(CatchUpPercent(26, 30, s) == 125);
    assert(CatchUpPercent(20, 30, s) == 275);
    assert(CatchUpPercent(1, 60, s) == 400);

    // Pace x bonus, under the total cap with RandomBotXPRate, never under the pace.
    uint64_t fast = 0, late = 0;
    for (uint64_t g = 1; !fast || !late; ++g)
    {
        if (!fast && PaceOf(g) == Pace::Fast)
            fast = g;
        if (!late && PaceOf(g) == Pace::Late)
            late = g;
    }
    assert(FreshStartPercent(fast, 20, 30, false, s, 200) == 150);
    assert(FreshStartPercent(fast, 28, 30, true, s, 200) == 150);  // no bonus: the pace
    assert(FreshStartPercent(late, 20, 30, true, s, 100) == 165);  // 60 % x 275 %
    assert(FreshStartPercent(fast, 1, 60, true, s, 200) == 300);   // 150 x 400 = 600, capped at 600 / 2
    assert(FreshStartPercent(fast, 1, 60, true, s, 500) == 150);   // the cap never goes under the pace
    CatchUpSettings noCap = s;
    noCap.maxTotalPercent = 0;
    assert(FreshStartPercent(fast, 1, 60, true, noCap, 200) == 600);

    // Percentiles of a histogram by level.
    std::vector<uint32_t> h(61, 0);
    h[10] = 50;
    h[20] = 40;
    h[60] = 10;
    assert(LevelAtPercentile(h, 10) == 10 && LevelAtPercentile(h, 50) == 10 && LevelAtPercentile(h, 51) == 20);
    assert(LevelAtPercentile(h, 90) == 20 && LevelAtPercentile(h, 100) == 60);
    assert(LevelAtPercentile(std::vector<uint32_t>(61, 0), 50) == 0);

    // Who counts for the front: a character really played within the active days.
    int64_t const since = 1000000;
    assert(!PlayedCharacter(false, 0, 0, since));             // never entered (the auction house seller, level 80)
    assert(!PlayedCharacter(true, 0, 0, since));              // held online by a tool, never played
    assert(!PlayedCharacter(false, 0, since + 50, since));    // a logout date but no play time
    assert(!PlayedCharacter(false, 3600, since - 1, since));  // played, but before the active days
    assert(PlayedCharacter(false, 3600, since, since));
    assert(PlayedCharacter(true, 60, 0, since));              // a first session, saved once
    // Whose accounts count: Malfurion, a living realm of the launcher with no player: LOCAL (GM), AHBOT (the
    // seller) and DCDRIVER (the Dungeon Clear driver, level 1, played 900 s there) must give no human.
    std::vector<std::string> const service = ServiceAccounts("AHBOT, dcdriver");
    assert(service.size() == 2 && service[1] == "DCDRIVER");
    assert(ServiceAccounts("").empty() && ServiceAccounts(" , ").empty());
    assert(!HumanAccount("LOCAL", false, true, "rndlv", service));
    assert(!HumanAccount("AHBOT", false, false, "rndlv", service));
    assert(!HumanAccount("DCDRIVER", false, false, "rndlv", service));    // never through the login server
    assert(!HumanAccount("DCDRIVER", true, false, "rndlv", service));     // and named a service account anyway
    assert(!HumanAccount("RNDLV154", true, false, "rndlv", service));     // a random bot account
    assert(!HumanAccount("SQUID", true, true, "rndlv", service, false));  // a GM, CountGameMasters = 0
    assert(HumanAccount("SQUID", true, false, "rndlv", service));
    // A solo server: the player plays the repack's GM account LOCAL (CountGameMasters = 1, the default), one
    // character played to level 12: one human, the front at 12.
    assert(HumanAccount("LOCAL", true, true, "rndlv", service));
    assert(!HumanAccount("AHBOT", true, true, "rndlv", service) && !HumanAccount("RNDLV1", true, true, "rndlv", service));
    assert(PlayedCharacter(false, 7200, since + 10, since) && MedianFront({12}, 1) == 12);
    assert(HumanAccount("SQUID", true, false, "", {}));
    // A realm with only the seller: no human, no front, no bonus.
    assert(MedianFront({}, 1) == 0 && CatchUpPercent(1, MedianFront({}, 1), s) == 100);
    return 0;
}
