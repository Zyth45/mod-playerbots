#include "CoaAltBagsRules.h"

#include <cassert>

int main()
{
    using namespace CoaAltBags;

    // The token follows every field and the order; the same list gives the same token.
    std::vector<Entry> list = {{255, 23, 2589, 5}, {1, 4, 4306, 20}};
    std::string const token = Token(list);
    assert(token.size() == 8 && token == Token(list));
    std::vector<Entry> more = list;
    more[0].count = 4;
    assert(Token(more) != token);
    std::vector<Entry> swapped = {list[1], list[0]};
    assert(Token(swapped) != token);
    assert(Token({}) != token);

    auto skip = ParseSkip("255:3,1:12,bad,7:,300:1,2:4");
    assert(skip.size() == 3 && skip.count({255, 3}) && skip.count({1, 12}) && skip.count({2, 4}));
    assert(ParseSkip("").empty());

    uint32_t id = 0;
    int32_t prop = 0;
    assert(ParseItem("2589", id, prop) && id == 2589 && prop == 0);
    assert(ParseItem("item:15231:0:0:0:0:0:-12:0:60", id, prop) && id == 15231 && prop == -12);
    assert(ParseItem("|cff1eff00|Hitem:15231:0:0:0:0:0:614:0:60|h[Gold Sword]|h|r", id, prop) && id == 15231 &&
           prop == 614);
    assert(ParseItem("item:6948", id, prop) && id == 6948 && prop == 0);
    assert(!ParseItem("sword", id, prop) && !ParseItem("item:abc", id, prop) && !ParseItem("", id, prop));

    assert(SlotsFor(1) == std::vector<uint8_t>{0});
    assert((SlotsFor(11) == std::vector<uint8_t>{10, 11}));
    assert((SlotsFor(17) == std::vector<uint8_t>{15, 16}));
    assert(SlotsFor(20) == SlotsFor(5));
    assert(SlotsFor(18).empty() && SlotsFor(0).empty());  // a bag, a non-equip item

    assert(std::string(Verdict(false, true, 1)) == "other");
    assert(std::string(Verdict(true, false, 1)) == "cannot");
    assert(std::string(Verdict(true, true, 1)) == "equip");
    assert(std::string(Verdict(true, true, 2)) == "replace");
    assert(std::string(Verdict(true, true, 0)) == "bad");

    Candidate grey;
    assert(Eligible(grey, 0, true) && Eligible(grey, 2, false));
    Candidate green;
    green.quality = 2;
    assert(!Eligible(green, 1, true) && Eligible(green, 2, true) && !Eligible(green, 3, true));
    Candidate blue;
    blue.quality = 3;
    assert(!Eligible(blue, 3, false));
    for (bool Candidate::*flag : {&Candidate::quest, &Candidate::useSpell, &Candidate::upgrade, &Candidate::kept,
                                  &Candidate::outfit, &Candidate::container, &Candidate::ammo, &Candidate::tool})
    {
        Candidate c;
        c.*flag = true;
        assert(!Eligible(c, 2, true) && !Eligible(c, 2, false));
    }
    Candidate conjured;
    conjured.conjured = true;
    assert(!Eligible(conjured, 2, true) && Eligible(conjured, 2, false));
    Candidate worthless;
    worthless.noPrice = true;
    assert(!Eligible(worthless, 2, true) && Eligible(worthless, 2, false));

    // Profession tools and materials: never in a list, for sale or to destroy.
    assert(IsProfessionItem(2, 20, 0));    // Fishing Pole (weapon, fishing pole)
    assert(IsProfessionItem(2, 14, 165));  // Mining Pick (weapon, misc, a tool category)
    assert(IsProfessionItem(2, 15, 166));  // Skinning Knife (dagger, a tool category)
    assert(IsProfessionItem(7, 12, 0));    // Runed Copper Rod, ore, leather, herbs (trade goods)
    assert(IsProfessionItem(5, 0, 0) && IsProfessionItem(13, 0, 0) && IsProfessionItem(3, 0, 0));
    assert(!IsProfessionItem(2, 7, 0));    // a plain one-handed sword
    assert(!IsProfessionItem(4, 1, 0));    // cloth armor
    Candidate pole;
    pole.quality = 1;
    pole.noPrice = true;
    pole.tool = IsProfessionItem(2, 20, 0);
    assert(!Eligible(pole, 1, false) && !Eligible(pole, 1, true));

    // Bag slots: the slot asked for, else the first empty one, else the smallest when the new bag is bigger.
    assert(PickBagSlot({6, 0, 10, 0}, 8, 0) == 1);
    assert(PickBagSlot({6, 10, 8, 12}, 14, 0) == 0);
    assert(PickBagSlot({10, 12, 14, 16}, 10, 0) == -1);  // no bigger: nothing replaced
    assert(PickBagSlot({10, 12, 14, 16}, 6, 3) == 2);    // the player's choice, whatever the size
    assert(PickBagSlot({10, 12, 14, 16}, 6, 5) == -1);
    // Emptying the old bag: every item somewhere, never two in one slot; nothing when one does not fit.
    std::vector<size_t> const to = AssignSlots(3, 4, [](size_t, size_t) { return true; });
    assert(to.size() == 3 && to[0] == 0 && to[1] == 1 && to[2] == 2);
    assert(AssignSlots(3, 2, [](size_t, size_t) { return true; }).empty());
    // A herb bag (slot 0) takes item 1 only: item 0 goes to slot 1.
    std::vector<size_t> const herb = AssignSlots(2, 2, [](size_t i, size_t s) { return s == 1 || i == 1; });
    assert(herb.size() == 2 && herb[0] == 1 && herb[1] == 0);
    assert(AssignSlots(0, 0, [](size_t, size_t) { return true; }).empty());

    // bagcheck: why a picked item may not go; conjured and no price only stop a sale; any quality.
    Candidate plain;
    plain.quality = 4;
    assert(std::string(Refusal(plain, true)) == "ok" && std::string(Refusal(plain, false)) == "ok");
    Candidate tool = plain;
    tool.tool = true;
    tool.noPrice = true;
    assert(std::string(Refusal(tool, false)) == "tool" && std::string(Refusal(tool, true)) == "tool");
    Candidate quest = plain;
    quest.quest = true;
    quest.tool = true;
    assert(std::string(Refusal(quest, false)) == "quest");
    Candidate wear = plain;
    wear.upgrade = true;
    assert(std::string(Refusal(wear, false)) == "wear");
    Candidate cheap = plain;
    cheap.noPrice = true;
    assert(std::string(Refusal(cheap, true)) == "noprice" && std::string(Refusal(cheap, false)) == "ok");
    Candidate conj = plain;
    conj.conjured = true;
    assert(std::string(Refusal(conj, true)) == "conjured" && std::string(Refusal(conj, false)) == "ok");
    for (auto field : {&Candidate::useSpell, &Candidate::kept, &Candidate::outfit, &Candidate::container, &Candidate::ammo})
    {
        Candidate c = plain;
        c.*field = true;
        assert(std::string(Refusal(c, false)) != "ok");
    }
    return 0;
}
