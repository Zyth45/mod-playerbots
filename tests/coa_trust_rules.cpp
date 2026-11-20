// CoA 3.1.6: the trust ranks and gains (CoaTrustRules.h).
#include "CoaTrustRules.h"

#include <cassert>
#include <cstdint>
#include <string>

using namespace CoaTrustRules;

int main()
{
    Settings s;
    assert(Gain(s, false, false) == 1 && Gain(s, false, true) == 1);
    assert(Gain(s, true, false) == 25 && Gain(s, true, true) == 100);

    assert(RankIndex(s, 0) == 0 && RankIndex(s, 99) == 0 && RankIndex(s, 100) == 1 && RankIndex(s, 499) == 1);
    assert(RankIndex(s, 500) == 2 && RankIndex(s, 1500) == 3 && RankIndex(s, 4000) == 4 && RankIndex(s, 9999) == 4);
    assert(RankIndex(s, 10000) == 5 && RankIndex(s, UINT32_MAX) == 5);

    assert(Line(s, 0) == "TRUST\tstranger\t0\t0\t100\tacquaintance");
    assert(Line(s, 120) == "TRUST\tacquaintance\t120\t100\t500\tcompanion");
    assert(Line(s, 4000) == "TRUST\ttrusted\t4000\t4000\t10000\tsworn");
    // The last rank: a full bar, no next rank.
    assert(Line(s, 12345) == "TRUST\tsworn\t12345\t10000\t12345\t-");

    assert(Add(10, 25) == 35 && Add(UINT32_MAX - 1, 100) == UINT32_MAX);

    // Ranks from the conf: six rising numbers from 0, or nothing changes.
    uint32_t from[RANK_COUNT] = {0, 100, 500, 1500, 4000, 10000};
    assert(ParseRanks("0, 50,200,800 ,2000,5000", from) && from[1] == 50 && from[3] == 800 && from[5] == 5000);
    Settings t;
    for (uint32_t i = 0; i < RANK_COUNT; ++i)
        t.from[i] = from[i];
    assert(Line(t, 60) == "TRUST\tacquaintance\t60\t50\t200\tcompanion");
    assert(!ParseRanks("0,100,500,1500,4000", from) && from[1] == 50);   // five
    assert(!ParseRanks("1,100,500,1500,4000,9000", from));              // not from 0
    assert(!ParseRanks("0,100,100,1500,4000,9000", from));              // not rising
    assert(!ParseRanks("0,100,abc,1500,4000,9000", from));
    assert(!ParseRanks("0,100,500,1500,4000,99999999999", from));
    assert(!ParseRanks("", from) && from[5] == 5000);
    return 0;
}
