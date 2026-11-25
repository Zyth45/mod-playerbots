#include "CoaMissionZoneRules.h"

#include <cassert>

namespace
{
struct StockBracket
{
    uint32_t low;
    uint32_t high;
};
}  // namespace

int main()
{
    using namespace CoaMissionZone;

    // Mission maps are their own list: Outland for alts, whatever the random bots are given.
    std::vector<uint32_t> const maps = {0, 1, 530};
    assert(OnMissionMap(maps, 0) && OnMissionMap(maps, 1) && OnMissionMap(maps, 530));
    assert(!OnMissionMap(maps, 571));
    assert(!OnMissionMap({}, 530));

    // A rescaled zone's band runs from its lowest to its highest budget level; level 0 rows are ignored.
    auto const scaled = ScaledBrackets({{3483, 60}, {3483, 58}, {3483, 63}, {3519, 60}, {3519, 0}});
    assert(scaled.size() == 2);
    assert(scaled.at(3483).low == 58 && scaled.at(3483).high == 63);
    assert(scaled.at(3519).low == 60 && scaled.at(3519).high == 60);

    // Rescaled band first (the stock Hellfire bracket would be 58-63 or 61-66 from the config), else the stock one,
    // else no mission zone (a Burning Crusade zone dropped on a level 60 realm and not rescaled).
    std::map<uint32_t, StockBracket> const stock = {{3483, {61, 66}}, {12, {1, 10}}};
    auto const hellfire = ZoneBracket(scaled, stock, 3483);
    assert(hellfire && hellfire->low == 58 && hellfire->high == 63);
    auto const elwynn = ZoneBracket(scaled, stock, 12);
    assert(elwynn && elwynn->low == 1 && elwynn->high == 10);
    assert(!ZoneBracket(scaled, stock, 3523));

    // A creature counts at its budget level, the highest if several zones list it; else at its template level.
    std::map<uint32_t, uint32_t> const levels = {{16878, 60}, {18077, 0}};
    assert(CreatureLevel(levels, 16878, 62) == 60);
    assert(CreatureLevel(levels, 18077, 64) == 64);
    assert(CreatureLevel(levels, 299, 7) == 7);
    return 0;
}
