#include "CoaDungeonClearLinkRules.h"

#include <cassert>
#include <string>

int main()
{
    using namespace CoaDcLink;

    // Only the exact request word.
    assert(IsCapsRequest("SQBW\tCAPS?"));
    assert(!IsCapsRequest("SQBW\tCAPS") && !IsCapsRequest("SQBW\tSOUL?") && !IsCapsRequest("CAPS?") &&
           !IsCapsRequest("SQBW\tCAPS?\tx"));

    // A classic CoA server: Dungeon Clear built in, announced or only seen through its run provider.
    assert(Present(true, true) && Present(true, false) && Present(false, true) && !Present(false, false));
    assert(CapsLine("3.1.4", true, "3.1.4") == "CAPS\tbots=3.1.4\tdc=1\tdcver=3.1.4");
    assert(CapsLine("3.1.4", true, "") == "CAPS\tbots=3.1.4\tdc=1\tdcver=-");
    assert(LogLine(true, "3.1.4") == "Dungeon Clear 3.1.4 is built into this server.");
    assert(LogLine(true, "") == "Dungeon Clear is built into this server.");

    // A server without it.
    assert(CapsLine("3.1.4", false, "3.1.4") == "CAPS\tbots=3.1.4\tdc=0\tdcver=-");
    assert(LogLine(false, "") ==
           "Dungeon Clear is not built into this server: the Dungeon features of SquidBots are unavailable.");

    // Versions never break a field or a line.
    assert(CleanVersion("3.1\t.4\n") == "3.1.4");
    assert(CleanVersion(std::string(100, 'x')).size() == VERSION_MAX);
    assert(CleanVersion("") == "-");
    assert(CapsLine("", true, "a\tb") == "CAPS\tbots=-\tdc=1\tdcver=ab");
    assert(CapsLine("3.1.4", true, std::string(300, 'v')).size() < 255 - 5);

    // One answer per player and second.
    assert(CapsAllowed(0, 0) && CapsAllowed(0, 5));
    assert(!CapsAllowed(1000, 1999) && CapsAllowed(1000, 2000));
    return 0;
}
