// CoA 3.3.1: the economy events' pure rules (CoaEconomyEventsRules.h).
#include "CoaEconomyEventsRules.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace CoaEconomyEventsRules;

int main()
{
    // Only the five known ways reach the database, as their own literal.
    for (char const* how : {"bags", "need", "drawn", "auction", "vendor"})
    {
        std::string const copy = how;  // another pointer, same text
        char const* known = KnownHow(copy.c_str());
        assert(known && copy == known && known != copy.c_str());
    }
    assert(!KnownHow(nullptr));
    assert(!KnownHow(""));
    assert(!KnownHow("bags'); DROP TABLE x; --"));
    assert(!KnownHow("Bags"));

    // Purge by age.
    assert(PurgeBefore(1000000, 90) == 1000000 - 90 * 86400);
    assert(PurgeBefore(1000000, 0) == 0);

    // Copper fits the column.
    assert(Copper(12345) == 12345u);
    assert(Copper(0x1FFFFFFFFull) == 0xFFFFFFFFu);

    std::puts("coa_economy_events_rules: OK");
    return 0;
}
