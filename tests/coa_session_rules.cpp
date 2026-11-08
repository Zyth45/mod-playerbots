#include "CoaSessionRules.h"

#include <cassert>
#include <random>

using namespace CoaSessions;

int main()
{
    std::mt19937 gen(7);
    auto between = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(gen); };

    // The curve: 24 values 0-100, quotes and spaces allowed; anything else gives the default.
    Curve c;
    assert(ParseCurve("", c) && c == DefaultCurve());
    assert(ParseCurve("\"10,10,10,10,10,10,10,10,10,10,10,10,50,50,50,50,50,50,100,100,100,100,100,100\"", c));
    assert(c[0] == 10 && c[12] == 50 && c[23] == 100);
    assert(!ParseCurve("1,2,3", c) && c == DefaultCurve());
    assert(!ParseCurve("10,10,10,10,10,10,10,10,10,10,10,10,50,50,50,50,50,50,100,100,100,100,100,101", c));
    assert(!ParseCurve("a,10,10,10,10,10,10,10,10,10,10,10,50,50,50,50,50,50,100,100,100,100,100,100", c));
    assert(!ParseCurve("10,10,10,10,10,10,10,10,10,10,10,10,50,50,50,50,50,50,100,100,100,100,100,100,5", c));

    // Between two hours, no jump; the weekend bonus; the floor; never above 100.
    Curve const& d = DefaultCurve();
    assert(HourPercent(d, 20, 0, 3, 10, 20) == 100);
    assert(HourPercent(d, 5, 0, 3, 10, 0) == 20);
    assert(HourPercent(d, 5, 30, 3, 10, 0) == 21);  // 20 -> 22
    assert(HourPercent(d, 5, 0, 6, 10, 0) == 30);   // Saturday
    assert(HourPercent(d, 5, 0, 0, 10, 0) == 30);   // Sunday
    assert(HourPercent(d, 20, 0, 0, 10, 0) == 100);
    assert(HourPercent(d, 5, 0, 3, 0, 40) == 40);
    assert(HourPercent(d, 23, 59, 3, 0, 0) == 56);  // 72 -> 55 at midnight
    assert(HourPercent(d, 0, 0, 3, 0, 0) == 55);
    for (uint32_t h = 0; h < 24; ++h)
        for (uint32_t m = 0; m < 60; m += 7)
        {
            uint32_t const now = HourPercent(d, h, m, 3, 0, 0);
            uint32_t const next = HourPercent(d, m + 1 < 60 ? h : h + 1, (m + 1) % 60, 3, 0, 0);
            assert(now <= 100 && (now > next ? now - next : next - now) <= 2);
        }

    assert(Target(500, 22) == 110 && Target(500, 100) == 500 && Target(3, 10) == 1 && Target(0, 50) == 0);

    // Sessions: mostly short or an evening, a few long, all within 15 min - 8 h.
    uint32_t shortS = 0, longS = 0;
    uint64_t total = 0;
    for (int i = 0; i < 10000; ++i)
    {
        uint32_t const s = SessionSeconds(between);
        assert(s >= 15 * 60 && s <= 8 * 3600);
        shortS += s <= 3600;
        longS += s > 5 * 3600;
        total += s;
    }
    assert(shortS > 4000 && shortS < 5000 && longS > 300 && longS < 700);
    assert(total / 10000 > 80 * 60 && total / 10000 < 120 * 60);  // about 1.5 h on average

    // Away: longer at a quiet hour, never more than 20 h.
    uint64_t quiet = 0, busy = 0;
    for (int i = 0; i < 5000; ++i)
    {
        uint32_t const q = OfflineSeconds(20, between), b = OfflineSeconds(100, between);
        assert(q >= 20 * 60 && q <= 20 * 3600 && b >= 20 * 60 && b <= 8 * 3600);
        quiet += q;
        busy += b;
    }
    assert(quiet > busy * 3 / 2);
    return 0;
}
