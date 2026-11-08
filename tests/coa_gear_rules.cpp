#include "CoaGearRules.h"

#include <cassert>

int main()
{
    // Ordinary gear: always allowed.
    assert(CoaGearAllowed(false, false));
    assert(CoaGearAllowed(false, true));
    // Bloodforged copy: only on a realm where Bloodforged is enabled.
    assert(!CoaGearAllowed(true, false));
    assert(CoaGearAllowed(true, true));
    return 0;
}
