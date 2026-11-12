#include "CoaWorldZoneLevels.h"

#include <cassert>
#include <set>

using namespace CoaWorldZones;

int main()
{
    // Every zone once, with a sane range.
    std::set<std::uint32_t> ids;
    for (ZoneLevels const& z : ZONES)
    {
        assert(ids.insert(z.zoneId).second);
        assert(z.minLevel >= 1 && z.minLevel <= z.maxLevel && z.maxLevel <= 60);
        assert(z.mapId == 0 || z.mapId == 1);
        assert(z.sides >= SIDE_ALLIANCE && z.sides <= SIDE_BOTH);
    }

    // Seen in a test: level 55 and 60 bots in Elwynn are far above it.
    assert(FarAbove(12, 60, 8));
    assert(FarAbove(12, 55, 8));
    assert(!FarAbove(12, 18, 8));
    assert(!FarAbove(1519, 60, 8));   // Stormwind City: a capital, passing through is fine
    assert(!FarAbove(139, 60, 8));    // Eastern Plaguelands at 60

    // Routes by side: Westfall is Alliance, the Barrens Horde, Stranglethorn both.
    assert(OnRoute(40, 15, false) && !OnRoute(40, 15, true));
    assert(OnRoute(17, 15, true) && !OnRoute(17, 15, false));
    assert(OnRoute(33, 38, false) && OnRoute(33, 38, true));
    assert(!OnRoute(33, 50, true));
    assert(!OnRoute(1637, 30, true));  // Orgrimmar: no range

    // Every level from 10 to 60 has a zone on each side's route, on both continents for most.
    for (std::uint8_t level = 10; level <= 60; ++level)
    {
        bool alliance = false, horde = false;
        for (ZoneLevels const& z : ZONES)
        {
            alliance |= OnRoute(z.zoneId, level, false);
            horde |= OnRoute(z.zoneId, level, true);
        }
        assert(alliance && horde);
    }

    // Density: a level 60 does not count as near a player in Elwynn; a level 8 does.
    assert(!Fits(12, 60, 8, 5));
    assert(Fits(12, 8, 8, 5));
    assert(Fits(1519, 58, 60, 5));   // a capital: the player's level decides
    assert(!Fits(1519, 20, 60, 5));
    return 0;
}
