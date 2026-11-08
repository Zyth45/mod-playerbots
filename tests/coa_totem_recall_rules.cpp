#include "CoaTotemRecallRules.h"

#include <cassert>

int main()
{
    CoaTotemRecallMoment const calm{true, true, false, false, false, false, false, 2, 5000, true};
    assert(CoaTotemRecallDecide(calm) == CoaTotemRecallDecision::CastRecall);
    CoaTotemRecallMoment m = calm;
    m.knowsRecall = false;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::Unsummon);

    m = calm;
    m.enabled = false;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.alive = false;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.inCombat = true;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.groupInCombat = true;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.bossInProgress = true;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.inDcRun = true;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.busy = true;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.totemsOut = 0;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m.knowsRecall = false;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m = calm;
    m.totemsOut = 1;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::CastRecall);

    // The delay after the last fight.
    m = calm;
    m.msSinceCombat = 4999;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m.knowsRecall = false;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m.msSinceCombat = CoaTotemRecallDelayMs;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::Unsummon);
    m.msSinceCombat = 0;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::None);
    m.msSinceCombat = 60000;
    assert(CoaTotemRecallDecide(m) == CoaTotemRecallDecision::Unsummon);

    static_assert(CoaTotemRecallDelayMs == 5000);
    static_assert(CoaTotemRecallCheckMs == 1000);
    return 0;
}
