#include "CoaSoulAddonRules.h"

#include <cassert>

using namespace CoaSoulAddon;

int main()
{
    std::string name = "x";
    assert(ParseRequest("SQBW\tSOUL?", name) && name.empty());
    assert(ParseRequest("SQBW\tSOUL?\tThrall", name) && name == "Thrall");
    assert(ParseRequest("SQBW\tSOUL?\tJaina\textra", name) && name == "Jaina");
    assert(!ParseRequest("SQBW\tSOULS?", name) && !ParseRequest("SQBA\tSOUL?", name) && !ParseRequest("SOUL?", name));
    assert(!ParseRequest("SQBW\tGUILD?", name));

    CoaSoul::Soul s;
    s.archetype = CoaSoul::TRADER;
    s.pace = 12;
    s.sociability = 80;
    s.curiosity = 33;
    s.greed = 90;
    s.aggression = 5;
    assert(Line("Ellalysia", s) == "SOUL\tEllalysia\ttrader\t12\t80\t33\t90\t5");
    assert(Line(std::string(300, 'a'), s).size() == 250);
    assert(NotYours() == "ERR\tSOUL?\tnotyours");

    Throttle t;
    assert(t.Allow(1, 100, 1000));
    assert(!t.Allow(1, 101, 1500));    // one request a second
    assert(t.Allow(1, 101, 2100));
    assert(!t.Allow(1, 100, 3200));    // the same bot within 10 s
    assert(t.Allow(1, 100, 11100));
    assert(t.Allow(2, 100, 11200));    // another player
    Throttle m;
    uint64_t now = 100000;
    for (uint64_t b = 0; b < 20; ++b, now += 1000)
        assert(m.Allow(7, b, now));
    assert(!m.Allow(7, 99, now));      // 20 a minute
    assert(m.Allow(7, 99, 100000 + 60000));
    return 0;
}
