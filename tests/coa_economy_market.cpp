#include "CoaEconomyRules.h"

#include <cassert>

using namespace CoaEconomyRules;

int main()
{
    // First sale: the band is that price.
    Band band;
    Learn(band, 1000);
    assert(band.usual == 1000 && band.low == 1000 && band.high == 1000 && band.sales == 1);

    // A dearer sale: usual moves 20 %, the high edge 30 %, low <= usual <= high.
    Learn(band, 2000);
    assert(band.usual == 1200 && band.high == 1300 && band.sales == 2);
    assert(band.low <= band.usual && band.usual <= band.high);

    // A cheaper sale: the low edge moves toward it.
    Learn(band, 500);
    assert(band.low < 1000 && band.low <= band.usual && band.usual <= band.high);

    // Outliers: ignored once 3 sales back the usual price, never before.
    Band thin{1000, 1000, 1000, 2};
    assert(!IsOutlier(thin, 100000));
    Band backed{1000, 900, 1100, 3};
    assert(IsOutlier(backed, 4001) && IsOutlier(backed, 249));
    assert(!IsOutlier(backed, 4000) && !IsOutlier(backed, 250));

    // Weight: grows with sales up to the max, halves after the half-life.
    assert(LearnedWeight(0, 5, 0.0f, 3.0f, 0.8f) == 0.0f);
    assert(LearnedWeight(5, 5, 0.0f, 3.0f, 0.8f) == 0.8f);
    float const half = LearnedWeight(10, 5, 3.0f, 3.0f, 0.8f);
    assert(half > 0.399f && half < 0.401f);

    // Blend: nothing learned keeps the formula; the learned price stays within 50-300 % of the formula.
    assert(Blend(1000, 0, 0.8f, 0.5f, 3.0f) == 1000);
    assert(Blend(1000, 2000, 0.0f, 0.5f, 3.0f) == 1000);
    assert(Blend(1000, 2000, 1.0f, 0.5f, 3.0f) == 2000);
    assert(Blend(1000, 100000, 1.0f, 0.5f, 3.0f) == 3000);
    assert(Blend(1000, 1, 1.0f, 0.5f, 3.0f) == 500);

    // The blend grows with the formula price whatever the learned price: the no-arbitrage cap stays valid.
    for (std::uint64_t usual : {1ull, 700ull, 5000ull, 999999ull})
        for (std::uint64_t f = 100; f < 20000; f += 37)
            assert(Blend(f, usual, 0.8f, 0.5f, 3.0f) <= Blend(f + 37, usual, 0.8f, 0.5f, 3.0f));
    return 0;
}
