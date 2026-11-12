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
    return 0;
}
