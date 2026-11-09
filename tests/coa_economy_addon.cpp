// CoA Economy: the pure rules of the artisans' addon protocol (Craft/CoaArtisansAddonRules.h).
#include "Craft/CoaArtisansAddonRules.h"

#include <cassert>
#include <cstdio>

using namespace CoaArtisansAddonRules;

int main()
{
    auto f = SplitTabs("AQUOTE\tBob\t2459\t3");
    assert(f.size() == 4 && f[0] == "AQUOTE" && f[1] == "Bob" && f[3] == "3");
    assert(SplitTabs("ALIST").size() == 1);
    assert(SplitTabs("ARCP\t").size() == 2 && SplitTabs("ARCP\t")[1].empty());

    assert(ParseNumber("2459") == 2459);
    assert(ParseNumber("") == 0 && ParseNumber("3x") == 0 && ParseNumber("-1") == 0 && ParseNumber("1234567890") == 0);

    assert(!QtyOk(0) && QtyOk(1) && QtyOk(20) && !QtyOk(21));
    assert(Stacks(20, 20) == 1 && Stacks(21, 20) == 2 && Stacks(3, 1) == 3 && Stacks(5, 0) == 5);

    assert(!QuoteExpired(1000, 1000 + 600) && QuoteExpired(1000, 1000 + 601));

    assert(!PriceRose(1000, 1100) && PriceRose(1000, 1101) && !PriceRose(1000, 900));

    assert(RateAllowed(0, 0, true));
    assert(!RateAllowed(5000, 5999, false) && RateAllowed(5000, 6000, false));
    assert(CacheFresh(1000, 10999) && !CacheFresh(1000, 11000));

    std::string const longLine(400, 'x');
    assert(Wire(longLine).size() == MaxLine && Wire("-") == "SQBW\t-");
    assert(Clean("a\tb\nc") == "a b c");

    assert(NextState("preparing", true, true, true, 99) == "preparing");
    assert(NextState("sent", false, true, false, 1) == "sent");
    assert(NextState("sent", false, true, true, 1) == "awaiting");
    assert(NextState("awaiting", true, false, true, 50) == "paid");
    assert(NextState("sent", false, false, false, 3) == "sent");       // not in the mailbox yet
    assert(NextState("awaiting", false, false, true, 50) == "returned");
    assert(NextState("paid", false, false, false, 50) == "paid");
    assert(IsFinal("cancelled") && !IsFinal("awaiting"));

    std::puts("coa_economy_addon PASS");
    return 0;
}
