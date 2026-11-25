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

    // The worn source: the noted origin; a made piece "selfcraft" on its maker, "craft" on anyone else; "start"
    // only under the factory or the auto-gear; never a guess.
    assert(std::string(WornSource("loot", false, false, false)) == "loot");
    assert(std::string(WornSource("auction", true, false, false)) == "auction");
    assert(std::string(WornSource("auction", false, true, false)) == "auction");  // a crafted piece bought
    assert(std::string(WornSource("craft", false, true, true)) == "selfcraft");
    assert(std::string(WornSource("craft", false, true, false)) == "craft");      // a mail order, a trade
    assert(std::string(WornSource(nullptr, false, true, true)) == "selfcraft");   // signed by its wearer
    assert(std::string(WornSource(nullptr, true, true, false)) == "craft");       // signed by another
    assert(std::string(WornSource(nullptr, true, false, false)) == "start");
    assert(WornSource(nullptr, false, false, false) == nullptr);  // a piece worn, its acquisition not seen: no row

    // coa_item_event: 20 per character; the same piece worn again within a minute writes nothing.
    assert(EventsKept == 20);
    assert(!RepeatPose(0, 1000));
    assert(RepeatPose(1000, 1000) && RepeatPose(1000, 1059));
    assert(!RepeatPose(1000, 1060) && !RepeatPose(1000, 999));

    // A factory pass closes 5 s after its last piece, or when the server stops.
    assert(!PassClosed(1000, 1000, false) && !PassClosed(1000, 1004, false));
    assert(PassClosed(1000, 1005, false) && PassClosed(1000, 1000, true));

    // An enchant event only when the enchantment changes.
    assert(NewEnchant(0, 1900) && NewEnchant(1887, 1900));
    assert(!NewEnchant(1900, 1900) && !NewEnchant(1900, 0));
    return 0;
}
