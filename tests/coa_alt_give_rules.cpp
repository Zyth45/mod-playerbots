// CoA 3.1.6: the addon's "give <itemId> <count> <alt>", the alt's name may hold a space (CoaAltGiveRules.h).
#include "CoaAltGiveRules.h"

#include <cassert>

using namespace CoaAltGiveRules;

int main()
{
    // The 3.1.6 trial: shown in the group as "Alt Mining", that is the character's name; read as "Alt" before.
    Give g = Parse("4338 20 Alt Mining");
    assert(g.itemId == 4338 && g.count == 20 && g.name == "Alt Mining");
    g = Parse("4338 1 Lethear Alt");
    assert(g.name == "Lethear Alt");
    g = Parse("4338 1 Squid");
    assert(g.name == "Squid");  // a one-word name as before
    g = Parse("4338 1   Dez Alt \r\n");
    assert(g.name == "Dez Alt");  // trimmed
    g = Parse("4338 1");
    assert(g.itemId == 4338 && g.name.empty());  // no name: "missing"
    g = Parse("abc 1 Alt Mining");
    assert(g.itemId == 0 && g.name.empty());  // not a number: "missing"
    g = Parse("");
    assert(g.itemId == 0 && g.count == 0 && g.name.empty());
    return 0;
}
