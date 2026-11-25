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
    assert(std::string(WornSource(nullptr, "loot", false, false, false)) == "loot");
    assert(std::string(WornSource(nullptr, "auction", true, false, false)) == "auction");
    assert(std::string(WornSource(nullptr, "auction", false, true, false)) == "craft");  // a crafted piece bought
    assert(std::string(WornSource(nullptr, "craft", false, true, true)) == "selfcraft");
    assert(std::string(WornSource(nullptr, "craft", false, true, false)) == "craft");   // a mail order
    assert(std::string(WornSource("trade", "craft", false, true, false)) == "craft");   // a crafted piece traded
    assert(std::string(WornSource("trade", "loot", false, false, false)) == "trade");   // looted, then traded
    assert(std::string(WornSource("guildbank", nullptr, false, false, false)) == "guildbank");
    assert(std::string(WornSource(nullptr, nullptr, false, true, true)) == "selfcraft");  // signed by its wearer
    assert(std::string(WornSource(nullptr, nullptr, true, true, false)) == "craft");      // signed by another
    assert(std::string(WornSource(nullptr, nullptr, true, false, false)) == "start");
    assert(std::string(WornSource(nullptr, "mail", true, false, false)) == "mail");
    assert(WornSource(nullptr, nullptr, false, false, false) == nullptr);  // worn before the update: no row

    // coa_item_event: 20 per character; the same piece worn again within a minute writes nothing.
    assert(EventsKept == 20);
    assert(!RepeatPose(0, 1000));
    assert(RepeatPose(1000, 1000) && RepeatPose(1000, 1059));
    assert(!RepeatPose(1000, 1060) && !RepeatPose(1000, 999));
    assert(SkipPose(true, 0, 5000) && SkipPose(false, 1000, 1030) && !SkipPose(false, 1000, 1060));
    assert(KeepEvents(20) == 20 && KeepEvents(0) == 10 && KeepEvents(5000) == 200);

    // Loot: "dungeon" in a dungeon or a raid, "loot" elsewhere; both fit the 16 characters of `source`.
    assert(std::string(LootSource(true, false)) == "dungeon" && std::string(LootSource(false, false)) == "loot");
    assert(std::string(LootSource(true, true)) == "raid");
    assert(std::string("guildbank").size() <= 16 && std::string("dungeonsim").size() <= 16);

    // A factory pass closes 5 s after its last piece, or when the server stops.
    assert(!PassClosed(1000, 1000, false) && !PassClosed(1000, 1004, false));
    assert(PassClosed(1000, 1005, false) && PassClosed(1000, 1000, true));

    // An enchant event only when the enchantment changes.
    assert(NewEnchant(0, 1900) && NewEnchant(1887, 1900));
    assert(!NewEnchant(1900, 1900) && !NewEnchant(1900, 0));
    return 0;
}
