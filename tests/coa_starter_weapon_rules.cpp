// CoA 3.1.6: the starting weapon of a self-found bot left with none, once (CoaStarterWeaponRules.h).
#include "CoaStarterWeaponRules.h"

#include <cassert>

using namespace CoaStarterWeaponRules;

int main()
{
    View v;
    v.enabled = true;
    v.selfFoundRealm = true;
    v.randomBot = true;
    assert(Gives(v));  // no weapon anywhere, never given: given

    View w = v;
    w.givenBefore = true;
    assert(!Gives(w));  // once only: sold or destroyed since, never again
    w = v;
    w.hasWeapon = true;
    assert(!Gives(w));  // a weapon worn or in the bags
    w = v;
    w.selfFoundRealm = false;
    assert(!Gives(w));  // only on a self-found realm
    w = v;
    w.randomBot = false;
    assert(!Gives(w));  // never a player's alt
    w = v;
    w.enabled = false;
    assert(!Gives(w));
    return 0;
}
