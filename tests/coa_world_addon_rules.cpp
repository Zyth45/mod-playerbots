#include "CoaWorldAddonRules.h"

#include <cassert>

using namespace CoaWorldAddonRules;

int main()
{
    auto r = Request("SQBW\tGORD\t2589\t5");
    assert(r.size() == 3 && r[0] == "GORD" && r[1] == "2589" && r[2] == "5");
    assert(Request("SQBW\tWEV?").size() == 1);
    assert(Request("SQBA\tGLIST").empty());
    assert(Request("SQBW").empty());

    assert(Mine("GORD") && Mine("GLIST") && Mine("WEV?") && Mine("GOFF") && Mine("GACC") && Mine("GREF") && Mine("GCAT"));
    assert(!Mine("ALIST") && !Mine("SOUL?") && !Mine("gord"));

    assert(RateOk(0, 5) && !RateOk(1000, 1999) && RateOk(1000, 2000));
    assert(!CacheFresh(0, 5) && CacheFresh(1000, 10999) && !CacheFresh(1000, 11000));
    assert(PushWanted(1000, 1000 + 1799) && !PushWanted(1000, 1000 + 1800) && !PushWanted(0, 10));

    assert(Number("12") == 12 && Number("") == 0 && Number("1a") == 0 && Number("-3") == 0 && Number("1234567890") == 0);
    assert(Quantity("5", 20) == 5 && Quantity("21", 200) == 0 && Quantity("2", 1) == 0 && Quantity("1", 1) == 1);
    assert(Quantity("0", 20) == 0 && Quantity("20", 0) == 0);

    assert(Text("") == "-" && Text("a\tb") == "a b");
    assert(Cut(std::string(400, 'x')).size() == MAX_LINE && Cut("ab") == "ab");

    assert(InvasionVisible(true, false, 30, 30) && !InvasionVisible(false, false, 80, 1) && !InvasionVisible(false, true, 9, 10));
    assert(RushVisible(30, 35, 5) && RushVisible(40, 35, 5) && !RushVisible(29, 35, 5));
    return 0;
}
