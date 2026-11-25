#include "CoaWorldItemOriginRules.h"

#include <cassert>
#include <string>

using namespace CoaWorldItemOriginRules;

int main()
{
    // Green and better pieces of the 19 equipment slots; not a bag, ammunition, a quiver nor a plain item.
    assert(ArmoryPiece(2, 5, 2) && ArmoryPiece(4, 13, 2) && ArmoryPiece(3, 2, 2));
    assert(!ArmoryPiece(1, 5, 2) && !ArmoryPiece(0, 5, 2));
    assert(!ArmoryPiece(3, 0, 2) && !ArmoryPiece(3, 18, 2) && !ArmoryPiece(3, 24, 2) && !ArmoryPiece(3, 27, 2));
    assert(ArmoryPiece(1, 5, 1));

    // Level bands of the hourly line.
    assert(LevelBand(1) == 0 && LevelBand(9) == 0 && LevelBand(10) == 10 && LevelBand(19) == 10 && LevelBand(60) == 60);

    // Purge: 90 days back, 0 = never.
    assert(PurgeBefore(10000000, 90) == 10000000 - 90 * 86400);
    assert(PurgeBefore(10000000, 0) == 0);

    // The hourly line, bands and sources in order.
    HourCounts counts;
    assert(HourLine(counts).empty());
    counts[10]["quest"] = 2;
    counts[10]["loot"] = 12;
    counts[0]["start"] = 3;
    assert(HourLine(counts) == "1-9: 3 start; 10-19: 12 loot, 2 quest");

    // The worn source: the noted origin; "start" only under the factory or the auto-gear; never a guess.
    assert(std::string(WornSource("loot", false, false)) == "loot");
    assert(std::string(WornSource("auction", true, false)) == "auction");
    assert(std::string(WornSource(nullptr, true, false)) == "start");
    assert(WornSource(nullptr, false, false) == nullptr);  // a piece worn, its acquisition not seen: no row
    assert(WornSource(nullptr, true, true) == nullptr);    // made by its wearer: no row
    return 0;
}
