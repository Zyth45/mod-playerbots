#include "CoaTinkerModule.h"

#include <cassert>

int main()
{
    using M = CoaModule;
    // One Tinker: Mana Module with mana casters or healers in the group, else Power Module.
    assert(CoaModuleChoice(true, 0, true, true) == M::Mana);
    assert(CoaModuleChoice(false, 0, true, true) == M::Power);
    // Two Tinkers take one kind each; a third takes the first kind again.
    assert(CoaModuleChoice(true, 1, true, true) == M::Power);
    assert(CoaModuleChoice(false, 1, true, true) == M::Mana);
    assert(CoaModuleChoice(true, 2, true, true) == M::Mana);
    // A Tinker that knows one kind keeps it, whatever the group.
    assert(CoaModuleChoice(true, 0, false, true) == M::Power);
    assert(CoaModuleChoice(false, 0, true, false) == M::Mana);
    assert(CoaModuleChoice(true, 1, true, false) == M::Mana);
    assert(CoaModuleChoice(false, 0, false, false) == M::None);
    // A single target Module goes to whom it helps.
    assert(CoaModuleProfits(M::Mana, true));
    assert(!CoaModuleProfits(M::Mana, false));
    assert(CoaModuleProfits(M::Power, false));
    assert(!CoaModuleProfits(M::Power, true));
    assert(!CoaModuleProfits(M::None, false));
}
