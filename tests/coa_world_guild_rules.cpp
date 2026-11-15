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

    // The count next to the link in guild chat (Silver Bar, item 2842).
    std::string const bar = "|cffffffff|Hitem:2842:0:0:0:0:0:0:0:20|h[Silver Bar]|h|r";
    assert(OrderedQty("can someone make 20 " + bar) == 20);
    assert(OrderedQty("need 20x " + bar) == 20);
    assert(OrderedQty("need x20 " + bar) == 20);
    assert(OrderedQty("need " + bar + " x20") == 20);
    assert(OrderedQty("need " + bar + " 20 please") == 20);
    assert(OrderedQty("need " + bar + "x5?") == 5);
    assert(OrderedQty("anyone have " + bar + "?") == 0);
    assert(OrderedQty("need " + bar + " for my level 30 quest") == 0);
    assert(OrderedQty("need " + bar + " 200") == 0);
    assert(OrderedQty("need 0 " + bar) == 0);
    assert(OrderedQty("level40 " + bar) == 0);
    assert(OrderedQty("need 21 " + bar) == 0);
    assert(OrderedItem("can someone make 20 " + bar) == 2842);

    // Crafted orders: enough casts for the count, one stack at most; one cast with no count.
    assert(PlanGuildCraft(20, 1, 20).casts == 20 && PlanGuildCraft(20, 1, 20).sent == 20);
    assert(PlanGuildCraft(0, 1, 20).casts == 1 && PlanGuildCraft(0, 1, 20).sent == 1);
    assert(PlanGuildCraft(5, 2, 20).casts == 3 && PlanGuildCraft(5, 2, 20).sent == 6);
    assert(PlanGuildCraft(20, 1, 1).casts == 1 && PlanGuildCraft(20, 1, 1).sent == 1);
    assert(PlanGuildCraft(0, 5, 20).casts == 1 && PlanGuildCraft(0, 5, 20).sent == 5);
    // The auction house: the cheapest by unit up to the count, then one more for the rest; gold for them all.
    {
        std::vector<AuctionOffer> const ah = { { 5, 15380 }, { 20, 100000 }, { 1, 2000 }, { 5, 16000 }, { 0, 1 } };
        assert((PickAuctions(ah, 20, 1000000, 12) == std::vector<std::size_t>{ 2, 0, 3, 1 })); // 11, then a stack: 31
        assert((PickAuctions(ah, 10, 1000000, 12) == std::vector<std::size_t>{ 2, 0, 3 }));  // 1 + 5, then 5: 11
        assert((PickAuctions(ah, 11, 1000000, 12) == std::vector<std::size_t>{ 2, 0, 3 }));  // exactly 11
        assert((PickAuctions(ah, 0, 1000000, 12) == std::vector<std::size_t>{ 2 }));        // no count: one, the cheapest
        assert((PickAuctions(ah, 20, 40000, 12) == std::vector<std::size_t>{ 2, 0, 3 }));   // gold ceiling for them all
        assert(PickAuctions(ah, 20, 1000, 12).empty() && PickAuctions(ah, 20, 1000000, 0).empty());
        assert((PickAuctions(ah, 20, 1000000, 1) == std::vector<std::size_t>{ 2 }));
        assert((PickAuctions({ { 5, 100 }, { 5, 100 }, { 5, 100 } }, 20, 1000000, 2).size() == 2));  // one mail
    }

    // The combined order (3.1.4): what is missing, stacks taken (one splits), the best stocked holders first.
    assert(OrderMissing(20, 0) == 20 && OrderMissing(20, 5) == 15 && OrderMissing(20, 25) == 0);
    assert(OrderMissing(0, 0) == 1 && OrderMissing(0, 3) == 0);
    assert(OrderAsk(20, 5) == 15 && OrderAsk(0, 0) == 0 && OrderAsk(20, 20) == 0);
    {
        std::vector<StackTake> const t = TakeStacks({ 3, 20, 0, 7 }, 25, 12);
        assert(t.size() == 2 && t[0].index == 1 && t[0].count == 20 && t[1].index == 3 && t[1].count == 5);
        std::vector<StackTake> const one = TakeStacks({ 3, 20 }, 1, 12);  // one out of the twenty
        assert(one.size() == 1 && one[0].index == 1 && one[0].count == 1);
        std::vector<StackTake> const any = TakeStacks({ 3, 20 }, 0, 12);  // any: the biggest stack, whole
        assert(any.size() == 1 && any[0].index == 1 && any[0].count == 20);
        std::vector<StackTake> const few = TakeStacks({ 2, 2, 2 }, 20, 2);  // one mail
        assert(few.size() == 2 && few[0].count + few[1].count == 4);
        assert(TakeStacks({}, 5, 12).empty() && TakeStacks({ 0, 0 }, 5, 12).empty() && TakeStacks({ 5 }, 5, 0).empty());
    }
    {
        // "5 in stock + 15 bought": two holders give 3 and 2, the rest goes on to the crafters and the AH.
        std::vector<StackTake> const s = SplitStock({ 2, 0, 3 }, 20);
        assert(s.size() == 2 && s[0].index == 2 && s[0].count == 3 && s[1].index == 0 && s[1].count == 2);
        std::vector<StackTake> const enough = SplitStock({ 4, 30, 9 }, 20);
        assert(enough.size() == 1 && enough[0].index == 1 && enough[0].count == 20);
        std::vector<StackTake> const any = SplitStock({ 4, 9 }, 0);
        assert(any.size() == 1 && any[0].index == 1 && any[0].count == 9);
        assert(SplitStock({ 0, 0 }, 5).empty());
    }

    // Materials without a price: one cast, as before (no twenty free items).
    assert(PlanGuildCraft(20, 1, 20, 0).casts == 1 && PlanGuildCraft(20, 1, 20, 0).sent == 1);
    assert(PlanGuildCraft(3, 5, 20, 0).casts == 1 && PlanGuildCraft(3, 5, 20, 0).sent == 3);
    assert(PlanGuildCraft(0, 5, 4, 0).sent == 4 && PlanGuildCraft(20, 1, 20, 150).casts == 20);

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
