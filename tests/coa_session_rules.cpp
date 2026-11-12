#include "CoaSessionRules.h"

#include <cassert>
#include <random>

using namespace CoaSessions;

// The bots online stay the bot count set: no hour curve is left, only the
// length of a bot's session and of its break while another one takes its place.
int main()
{
    std::mt19937 gen(7);
    auto between = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(gen); };

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

    // Away: 20 min to 8 h, whatever the hour.
    for (int i = 0; i < 5000; ++i)
    {
        uint32_t const away = OfflineSeconds(between);
        assert(away >= 20 * 60 && away <= 8 * 3600);
    }
    return 0;
}
