// CoA 3.1.6: the trust ranks and gains (CoaTrustRules.h).
#include "CoaTrustRules.h"

#include <cassert>
#include <cstdint>
#include <string>

using namespace CoaTrustRules;

int main()
{
    assert(Gain(false, false) == 1 && Gain(false, true) == 1);
    assert(Gain(true, false) == 25 && Gain(true, true) == 100);

    assert(RankIndex(0) == 0 && RankIndex(99) == 0 && RankIndex(100) == 1 && RankIndex(499) == 1);
    assert(RankIndex(500) == 2 && RankIndex(1500) == 3 && RankIndex(4000) == 4 && RankIndex(9999) == 4);
    assert(RankIndex(10000) == 5 && RankIndex(UINT32_MAX) == 5);

    assert(Line(0) == "TRUST\tstranger\t0\t0\t100\tacquaintance");
    assert(Line(120) == "TRUST\tacquaintance\t120\t100\t500\tcompanion");
    assert(Line(4000) == "TRUST\ttrusted\t4000\t4000\t10000\tsworn");
    // The last rank: a full bar, no next rank.
    assert(Line(12345) == "TRUST\tsworn\t12345\t10000\t12345\t-");

    assert(Add(10, 25) == 35 && Add(UINT32_MAX - 1, 100) == UINT32_MAX);
    return 0;
}
