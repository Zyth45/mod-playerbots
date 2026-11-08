#include "CoaPetRules.h"

#include <cassert>
#include <set>

using namespace CoaPet;

int main()
{
    // The table: 8 summon spells (Book of Ascension has two), each category present, ids unique.
    std::set<uint32_t> spells, entries;
    int perCat[CAT_COUNT] = {};
    for (Companion const& c : Companions())
    {
        assert(spells.insert(c.spellId).second && entries.insert(c.entry).second);
        ++perCat[c.category];
    }
    assert(Companions().size() == 8 && perCat[CAT_LOOT] == 2 && perCat[CAT_QUEST] == 1 && perCat[CAT_CLASS] == 4 &&
           perCat[CAT_CRAFT] == 1);
    assert(BySpell(83050)->entry == 79025 && ByEntry(57500)->spellId == 750750 && !BySpell(1));

    // Commands.
    assert(Parse("").verb == Verb::None);
    assert(Parse("attack").verb == Verb::None && Parse("follow").verb == Verb::None);  // the hunter's pet command
    assert(Parse("list").verb == Verb::List && Parse("list").valid);
    Command c = Parse("set class 105307");
    assert(c.verb == Verb::Set && c.valid && c.category == CAT_CLASS && c.spellId == 105307);
    c = Parse("set craft 0");
    assert(c.valid && c.category == CAT_CRAFT && c.spellId == 0);
    assert(!Parse("set class 750750").valid);  // a craft companion
    assert(!Parse("set pets 0").valid && !Parse("set class").valid);
    c = Parse("use quest");
    assert(c.verb == Verb::Use && c.valid && c.category == CAT_QUEST && c.spellId == 0);
    c = Parse("use 9930889");
    assert(c.valid && c.category == CAT_LOOT && c.spellId == 9930889);
    assert(!Parse("use 123").valid);
    assert(Parse("dismiss").valid);
    c = Parse("auto off");
    assert(c.verb == Verb::Auto && c.valid && !c.on);
    assert(Parse("  auto   on ").on && !Parse("auto maybe").valid);

    // The choice: the favourite when known, else the first known of the category.
    auto knowsBooks = [](uint32_t s) { return s == 979602 || s == 105307; };
    assert(Choose(CAT_CLASS, 105307, knowsBooks) == 105307);
    assert(Choose(CAT_CLASS, 499908, knowsBooks) == 979602);  // favourite not known
    assert(Choose(CAT_CLASS, 750750, knowsBooks) == 979602);  // not a class companion
    assert(Choose(CAT_CRAFT, 0, knowsBooks) == 0);

    // Stored choice, and anything unreadable gives the defaults.
    Choice ch;
    ch.favourite[CAT_QUEST] = 83050;
    ch.favourite[CAT_CRAFT] = 750750;
    ch.autoOn = false;
    assert(Store(ch) == "83050,0,750750,0");
    Choice back = Load(Store(ch));
    assert(back.favourite[CAT_QUEST] == 83050 && back.favourite[CAT_CLASS] == 0 && back.favourite[CAT_CRAFT] == 750750 &&
           !back.autoOn);
    assert(Load("").autoOn && Load("1,2").favourite[CAT_QUEST] == 0 && Load("a,b,c,d").autoOn);

    // Lines.
    assert(PetLine(*BySpell(83050), true, "Wondrous Wisdomball") == "PET\tpet\t83050\t79025\tquest\t1\tWondrous Wisdomball");
    assert(FavLine(CAT_LOOT, 0) == "PET\tfav\tloot\t0");
    assert(OutLine(750750) == "PET\tout\t750750" && AutoLine(true) == "PET\tauto\t1");
    assert(ErrLine("combat") == "PET\terr\tcombat" && EndLine() == "PET\t-");

    // Rank caps: the book only helps below the last rank.
    assert(RankCapReached(75, 75) && RankCapReached(225, 225) && !RankCapReached(74, 75));
    assert(!RankCapReached(300, 300) && !RankCapReached(0, 0));

    // Refusals, in the loot pet's order.
    assert(Refusal(false, true, false, false, false) == "unknown");
    assert(Refusal(true, false, true, false, false) == "dead");
    assert(Refusal(true, true, true, true, false) == "combat");
    assert(Refusal(true, true, false, true, false) == "mounted");
    assert(Refusal(true, true, false, false, true) == "raid");
    assert(Refusal(true, true, false, false, false).empty());
    return 0;
}
