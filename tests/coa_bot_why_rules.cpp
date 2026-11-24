// CoA 3.2: ".squidbots why" (CoaBotWhyRules.h).
#include "CoaBotWhyRules.h"

#include <cassert>

using namespace CoaBotWhyRules;

int main()
{
    assert(Duration(0) == "0s");
    assert(Duration(45000) == "45s");
    assert(Duration(192000) == "3m12s");
    assert(Duration(185000) == "3m05s");
    assert(Duration(7500000) == "2h05m");
    assert(!Moved(0.5f, 0.5f, 0.0f));
    assert(Moved(1.0f, 0.5f, 0.0f));
    assert(Moved(0.0f, 0.0f, -1.5f));
    return 0;
}
