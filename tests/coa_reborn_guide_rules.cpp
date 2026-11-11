#include "CoaRebornGuideRules.h"

#include <cassert>

int main()
{
    assert(WcrGuideMayRewrite(true, false));    // a random bot: the guide build, always
    assert(WcrGuideMayRewrite(false, true));    // an addclass bot: the guide build too
    assert(!WcrGuideMayRewrite(false, false));  // a player's alt (Matias' mage): its build is never rewritten
    return 0;
}
