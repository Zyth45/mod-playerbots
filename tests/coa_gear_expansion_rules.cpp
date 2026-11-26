// CoA 3.3.1: no Burning Crusade gear from the factory for a bot of a Classic realm (CoaGearExpansionRules.h), rows of
// the CoA item_template.
#include "CoaGearExpansionRules.h"

#include <cassert>

using CoaGearExpansionRules::LaterExpansion;

int main()
{
    // The player's report: Fel Iron Chain Coif (green, item level 90, level 60) on a level 60 bot.
    assert(LaterExpansion(23493, 2, 90, 60, 60, false));
    assert(LaterExpansion(23587, 2, 90, 60, 60, false));     // Mirren's Drinking Hat
    assert(LaterExpansion(17886, 3, 435, 60, 60, false));    // QA Test Ring
    assert(LaterExpansion(28000, 3, 100, 60, 60, false));    // a Burning Crusade id, as before
    // The original game's gear stays.
    assert(!LaterExpansion(10474, 3, 72, 60, 60, false));    // Drake Scale Drape, the top blue
    assert(!LaterExpansion(22806, 4, 86, 60, 60, false));    // Widow's Remorse (Naxxramas)
    assert(!LaterExpansion(21205, 4, 80, 60, 60, false));    // Signet Ring of the Bronze Dragonflight (Ahn'Qiraj)
    assert(!LaterExpansion(22589, 5, 87, 60, 60, false));    // Atiesh
    assert(!LaterExpansion(23464, 4, 90, 60, 60, false));    // High Warlord's Battle Mace (PvP 1.12)
    assert(!LaterExpansion(23244, 3, 71, 60, 60, false));    // Champion's Plate Helm (PvP 1.12)
    assert(!LaterExpansion(13377, 2, 61, 56, 60, false));    // Miniature Cannon Balls
    assert(!LaterExpansion(450648, 2, 23, 24, 60, true));    // a Worldforged item
    // A Dawnrise realm (61-80) keeps the Burning Crusade's gear.
    assert(!LaterExpansion(23493, 2, 90, 61, 80, false));
    assert(!LaterExpansion(28000, 3, 100, 65, 80, false));
    assert(LaterExpansion(40000, 4, 200, 70, 80, false));
    assert(!LaterExpansion(37739, 3, 130, 70, 80, false));
    assert(!LaterExpansion(40000, 4, 200, 71, 80, false));
    // A level 60 bot of a Wrath realm (ceiling 80) may wear the coif; a Burning Crusade id stays out, as before.
    assert(!LaterExpansion(23493, 2, 90, 60, 80, false));
    assert(LaterExpansion(28000, 3, 100, 60, 80, false));
    // The CoA copies of high ids (Fel Iron Chain Coif "Refined" 1153215) stay out on a Classic realm.
    assert(LaterExpansion(1153215, 2, 93, 60, 60, false));
    // Obsidian Mail Tunic (an Ahn'Qiraj craft) and Icebane Leggings (a quest reward of Naxxramas): epics, they stay.
    assert(!LaterExpansion(22191, 4, 76, 60, 60, false));
    assert(!LaterExpansion(22699, 4, 80, 60, 60, false));

    // The factory's bag: the Traveler's Backpack (16 slots) on a Classic realm, the Portable Hole (24) elsewhere.
    using CoaGearExpansionRules::BagSlot;
    using CoaGearExpansionRules::FactoryBag;
    using CoaGearExpansionRules::ForSlot;
    uint32_t const hole = CoaGearExpansionRules::PORTABLE_HOLE, classic = CoaGearExpansionRules::CLASSIC_BAG;
    assert(FactoryBag(true, 60) == classic);
    assert(FactoryBag(true, 80) == hole);
    assert(FactoryBag(false, 60) == hole);
    // An empty bag slot gets it; the same bag stays.
    assert(ForSlot(0, 0, false, classic, 16, true, true) == BagSlot::Fill);
    assert(ForSlot(classic, 16, true, classic, 16, true, true) == BagSlot::Keep);
    // A random bot's empty Portable Hole gives way, at a login (no destroyOld) as at a factory pass.
    assert(ForSlot(hole, 24, true, classic, 16, false, true) == BagSlot::Replace);
    assert(ForSlot(hole, 24, true, classic, 16, true, true) == BagSlot::Replace);
    // Never with anything inside, never a player's alt's.
    assert(ForSlot(hole, 24, false, classic, 16, true, true) == BagSlot::Keep);
    assert(ForSlot(hole, 24, true, classic, 16, true, false) == BagSlot::Keep);
    // Another bag: replaced at a factory pass only, empty, and only by a larger one (the Bottomless Bag stays).
    assert(ForSlot(4496, 6, true, classic, 16, true, true) == BagSlot::Replace);  // Small Brown Pouch
    assert(ForSlot(4496, 6, true, classic, 16, false, true) == BagSlot::Keep);
    assert(ForSlot(4496, 6, false, classic, 16, true, true) == BagSlot::Keep);
    assert(ForSlot(14156, 18, true, classic, 16, true, true) == BagSlot::Keep);
    // A Wrath realm: the Portable Hole stays the factory's bag.
    assert(ForSlot(hole, 24, true, hole, 24, true, true) == BagSlot::Keep);
    assert(ForSlot(classic, 16, true, hole, 24, true, true) == BagSlot::Replace);
    return 0;
}
