#include "CoaEconomyRules.h"

#include <cassert>

using namespace CoaEconomyRules;

int main()
{
    // Day 1: everybody level 1.
    std::map<std::uint32_t, std::uint32_t> day1{{1, 500}};
    assert(FrontLevel(day1, 80) == 1 && TopLevel(day1, 3) == 1);

    // Half-way: the mass around 25-30, a few fast ones at 45, two at 52 (not enough for the top).
    std::map<std::uint32_t, std::uint32_t> mid{{10, 50}, {25, 200}, {30, 200}, {45, 48}, {52, 2}};
    assert(FrontLevel(mid, 80) == 30);
    assert(TopLevel(mid, 3) == 45);

    // Late: three characters at 60.
    std::map<std::uint32_t, std::uint32_t> late{{40, 100}, {55, 300}, {60, 3}};
    assert(TopLevel(late, 3) == 60);
    assert(FrontLevel({}, 80) == 1);

    // Caps on day 1: low level whites and greens only, no blue, no epic.
    LaunchCaps caps;
    caps.front = 1;
    caps.top = 1;
    assert(LaunchAllows(caps, 0, 5, 1));    // white goods, item level 5
    assert(LaunchAllows(caps, 4, 8, 2));    // green, level 4
    assert(!LaunchAllows(caps, 5, 10, 2));  // level 5 > 1 + 3
    assert(!LaunchAllows(caps, 2, 10, 3));  // blue before front 20
    assert(!LaunchAllows(caps, 1, 10, 4));  // epic before anyone at 50
    assert(!LaunchAllows(caps, 0, 10, 1));  // goods of item level 10 > 1 + 3 + 5

    // Half-way: blues up to front + 3, still no epic.
    caps.front = 30;
    caps.top = 45;
    assert(LaunchAllows(caps, 33, 38, 3));
    assert(!LaunchAllows(caps, 34, 38, 3));
    assert(!LaunchAllows(caps, 30, 40, 4));

    // An epic only once the top has its level and is at least 50; never legendary unless allowed.
    caps.front = 55;
    caps.top = 52;
    assert(LaunchAllows(caps, 52, 60, 4));
    assert(!LaunchAllows(caps, 55, 60, 4));
    assert(!LaunchAllows(caps, 50, 60, 5));
    caps.allowLegendary = true;
    assert(LaunchAllows(caps, 50, 60, 5));

    // Goods within 5 item levels of the cap even with a required level; no profession rank out of reach.
    caps.front = 1;
    assert(LaunchAllowsGoods(caps, false, 4, 0) && !LaunchAllowsGoods(caps, false, 60, 0)); // a level 1 wine of item level 60
    assert(LaunchAllowsGoods(caps, true, 60, 0));                                          // gear: its required level rules
    assert(LaunchSkillCap(4) == 49 && LaunchSkillCap(15) == 115 && LaunchSkillCap(80) == 450);
    assert(!LaunchAllowsGoods(caps, false, 0, 300) && LaunchAllowsGoods(caps, false, 4, 1)); // rank 300 kit; rank 1 kit
    // One item's price: 1 g at front 1, 25 g at 10, none from 60 or when off.
    assert(LaunchUnitPriceCap(1, 0.25f, 60) == 10000 && LaunchUnitPriceCap(10, 0.25f, 60) == 250000);
    assert(LaunchUnitPriceCap(60, 0.25f, 60) == 0 && LaunchUnitPriceCap(10, 0.0f, 60) == 0);

    // Stock 10 % at the start to 100 % at max level; launch premium x2.5 fading to x1.
    assert(LaunchStockFactor(0.1f, 0.0f) == 0.1f && LaunchStockFactor(0.1f, 1.0f) == 1.0f);
    assert(LaunchPriceFactor(1.5f, 0.0f) == 2.5f && LaunchPriceFactor(1.5f, 1.0f) == 1.0f);
    assert(LaunchPriceFactor(1.5f, 0.5f) == 1.375f);
    return 0;
}
