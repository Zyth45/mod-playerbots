#include "CoaWorldGuildRules.h"

#include <cassert>

using namespace CoaWorldGuildRules;

int main()
{
    std::string const link = "|cff1eff00|Hitem:2589:0:0:0:0:0:0:0:20|h[Linen Cloth]|h|r";

    // Asking for an item.
    assert(OrderedItem("anyone have " + link + "?") == 2589);
    assert(OrderedItem("Need " + link + " for a quest") == 2589);
    assert(OrderedItem("can someone craft " + link) == 2589);
    assert(OrderedItem("lf " + link) == 2589);
    assert(OrderedItem(link + " anyone?") == 2589);

    // Showing an item, or talking without a link: nothing.
    assert(OrderedItem("look what I got " + link) == 0);
    assert(OrderedItem("anyone up for a dungeon?") == 0);
    // A word of asking inside the item's name does not count.
    assert(OrderedItem("|cffffffff|Hitem:1:0|h[Make Anyone Need]|h|r") == 0);
    assert(OrderedItem("need |Hitem:0:0|h[x]|h|r") == 0);

    // Materials at vendor price, else four times the vendor's buying price.
    assert(ReagentPrice(100, 25) == 100);
    assert(ReagentPrice(0, 25) == 100);

    // Orders a day.
    assert(MayOrder(0, 3) && MayOrder(2, 3) && !MayOrder(3, 3) && !MayOrder(0, 0));

    // Gear gifts: clearly better only, a gift every few days, of the player's level band.
    assert(WorthGiving(50.0f, 0.0f));
    assert(WorthGiving(130.0f, 100.0f));
    assert(!WorthGiving(110.0f, 100.0f));
    assert(!WorthGiving(0.0f, 0.0f));
    assert(GiftDue(20000, 0, 3) && GiftDue(20003, 20000, 3) && !GiftDue(20002, 20000, 3));
    assert(GiftLevelFits(30, 30) && GiftLevelFits(26, 30) && !GiftLevelFits(25, 30) && !GiftLevelFits(31, 30));

    // Cold-Iron Scepter (spell power only) is no gift for a physical spec, a strength piece none for a caster.
    assert(!RoleFits(false, 0, 1) && RoleFits(true, 0, 1));
    assert(!RoleFits(true, 2, 0) && RoleFits(false, 2, 0));
    assert(RoleFits(true, 0, 0) && RoleFits(false, 0, 0) && RoleFits(false, 1, 1) && RoleFits(true, 1, 1));

    assert(IsGreeting("hello") && IsGreeting("Hi all!") && IsGreeting("bonjour la guilde") && IsGreeting("re"));
    assert(IsGreeting("cc tout le monde") && IsGreeting("o/") && IsGreeting("hey guys :)"));
    assert(!IsGreeting("hi can someone help me with this quest please"));
    assert(!IsGreeting("this is high level")  && !IsGreeting("reroll time") && !IsGreeting(""));
    // The user's hellos in /g (3.0.1): every one is heard.
    for (char const* hello : { "yo", "hey", "hi", "o/", "salut", "bonjour", "coucou", "re", "Yo!", "hey all", "hi guys" })
        assert(IsGreeting(hello));
    // Goodbyes in /g.
    for (char const* bye : { "gn", "gn all", "bye", "bye guys", "cya", "cya tomorrow", "night all", "gtg, cya",
                             "bonne nuit", "bonne nuit tout le monde", "a+", "++", "A+ !", "a demain", "je deco" })
        assert(IsGoodbye(bye));
    for (char const* not_bye : { "hi", "re", "", "c++ is hard", "bye the way can someone help me with this quest",
                                 "the boss is down", "a+b", "nightmare" })
        assert(!IsGoodbye(not_bye));

    // 2 or 3 answers, fewer only when fewer mates are online; stand-ins split among the speakers, nobody twice.
    assert(AnswerCount(10, 0) == 2 && AnswerCount(10, 1) == 3 && AnswerCount(10, 7) == 3);
    assert(AnswerCount(1, 1) == 1 && AnswerCount(0, 0) == 0 && AnswerCount(2, 1) == 2);
    assert((StandIns(0, 3, 10) == std::vector<size_t>{ 3, 6, 9 }));
    assert((StandIns(1, 3, 10) == std::vector<size_t>{ 4, 7 }));
    assert((StandIns(2, 3, 10) == std::vector<size_t>{ 5, 8 }));
    assert(StandIns(0, 2, 2).empty() && StandIns(0, 0, 5).empty());
    {
        std::vector<time_t> const t = AnswerTimes({ 105, 103, 104 });
        assert(t[0] == 103 && t[1] == 105 && t[2] == 107);
    }

    // The answer to a hello: its own clock, a minute.
    assert(GreetReplyDue(0, 1000));
    assert(!GreetReplyDue(1000, 1059));
    assert(GreetReplyDue(1000, 1060));
    return 0;
}
