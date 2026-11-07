#include "CoaBotControlRules.h"

#include <cassert>

int main()
{
    // Any one reason is enough.
    assert(CoaMayControlBot(true, false, false, false, false));
    assert(CoaMayControlBot(false, true, false, false, false));
    assert(CoaMayControlBot(false, false, true, false, false));
    assert(CoaMayControlBot(false, false, false, true, false));
    assert(CoaMayControlBot(false, false, false, false, true));
    // An offline random bot of another account, outside the guild, not linked: refused before "ok".
    assert(!CoaMayControlBot(false, false, false, false, false));
    return 0;
}
