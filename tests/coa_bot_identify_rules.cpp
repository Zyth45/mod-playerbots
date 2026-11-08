#include "CoaBotIdentifyRules.h"

#include <cassert>

int main()
{
    assert(CoaIdentifyIsWho("SQBA\tWHO?"));
    assert(CoaIdentifyIsWho("SQBA\tWHO? "));
    assert(!CoaIdentifyIsWho("SQBA\tWHO"));
    assert(!CoaIdentifyIsWho("SQBA\tWHO?x"));
    assert(!CoaIdentifyIsWho("DBM\tWHO?"));
    assert(!CoaIdentifyIsWho("WHO?"));
    assert(!CoaIdentifyIsWho(""));

    assert(CoaIdentifyAnswer(true, "") == "IAM\trandom\t-");
    assert(CoaIdentifyAnswer(true, "Arthas") == "IAM\trandom\t-");
    assert(CoaIdentifyAnswer(false, "Arthas") == "IAM\talt\tArthas");
    assert(CoaIdentifyAnswer(false, "") == "IAM\talt\t-");
    assert(CoaIdentifyAnswer(false, "A\tB") == "IAM\talt\tA B");

    assert(!CoaIdentifyEnabled(false, 1));
    assert(CoaIdentifyEnabled(true, 1));
    assert(CoaIdentifyEnabled(false, 0));
    assert(CoaIdentifyEnabled(false, 2));

    CoaIdentifyThrottle t(5000, 4);
    assert(t.Allow(1, 9, 1000));
    assert(!t.Allow(1, 9, 5999));
    assert(t.Allow(2, 9, 5999));  // another bot
    assert(t.Allow(1, 8, 5999));  // another asker
    assert(t.Allow(1, 9, 6000));
    // Pruning keeps the live entries only.
    assert(t.Allow(3, 9, 6000));
    assert(t.Allow(4, 9, 20000));
    assert(t.Size() == 1);
    assert(!t.Allow(4, 9, 20001));
    return 0;
}
