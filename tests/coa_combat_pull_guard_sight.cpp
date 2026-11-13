// The body pull guard in a fight (src/Ai/Coa/Combat/PullGuard/CoaPullGuardSightRule.h).
#include "Ai/Coa/Combat/PullGuard/CoaPullGuardSightRule.h"

#include <cassert>

using namespace CoaPullGuardSightRule;

int main()
{
    Pack p;
    p.aggro = 20.0f;
    p.margin = 0.0f;
    p.fighting = true;
    p.sightRuleOn = true;
    p.botDist = 30.0f;

    p.destDist = 25.0f;  // outside the aggro distance: never a risk
    assert(!NeedsSight(p) && !IsRisk(p, true));

    p.destDist = 15.0f;  // inside, closer than now: the pack's sight decides
    assert(NeedsSight(p));
    assert(IsRisk(p, true));
    assert(!IsRisk(p, false));  // behind a wall from the pack: the bot may go there

    p.botDist = 12.0f;  // already closer than the destination: the move pulls nothing new
    assert(!NeedsSight(p) && !IsRisk(p, true));
    p.botDist = 30.0f;

    Pack off = p;
    off.sightRuleOn = false;  // key 0: the guard as it was, sight or not
    assert(!NeedsSight(off) && IsRisk(off, false));
    off.botDist = 12.0f;
    assert(IsRisk(off, false));

    Pack calm = p;
    calm.fighting = false;  // out of combat: unchanged, margin included
    calm.margin = 3.0f;
    calm.destDist = 22.0f;
    assert(!NeedsSight(calm) && IsRisk(calm, false));
    calm.destDist = 23.0f;
    assert(!IsRisk(calm, true));
    return 0;
}
