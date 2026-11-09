#include "CoaSheathRules.h"

#include <cassert>

using namespace CoaSheath;

int main()
{
    assert(Wanted(false, false, false, false, false) == UNARMED);  // in town
    assert(Wanted(true, false, false, false, false) == MELEE);     // a fight
    assert(Wanted(true, false, false, true, true) == RANGED);      // a hunter with its bow
    assert(Wanted(true, false, false, true, false) == MELEE);      // ranged but no ranged weapon: a wand-less caster
    assert(Wanted(true, true, false, false, false) == UNARMED);    // on a mount
    assert(Wanted(true, false, true, true, true) == UNARMED);      // in flight
    return 0;
}
