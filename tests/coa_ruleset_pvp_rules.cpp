#include "CoaRulesetPvpRules.h"

#include <cassert>

int main()
{
    // High Risk is a PvP ruleset, whatever the other auras; War Mode only without the PvE marker.
    assert(CoaInPvpRuleset(true, false, false));
    assert(CoaInPvpRuleset(true, false, true));
    assert(CoaInPvpRuleset(false, true, false));
    assert(!CoaInPvpRuleset(false, true, true));
    assert(!CoaInPvpRuleset(false, false, true));
    assert(!CoaInPvpRuleset(false, false, false));

    // Reset after a death or a random teleport, PvE realm: a High Risk bot keeps PvP on (the Discord report: High Risk
    // aura, no PvP flag), a PvE bot does not get it.
    assert(CoaBotPvpAfterReset(false, true));
    assert(!CoaBotPvpAfterReset(false, false));
    assert(CoaBotPvpAfterReset(true, false));

    // The flag follows the auras the bot has.
    assert(CoaRulesetPvpFlagChange(true, false, false) == 1);   // High Risk without the flag: set it
    assert(CoaRulesetPvpFlagChange(true, false, true) == 0);
    assert(CoaRulesetPvpFlagChange(false, true, true) == -1);   // PvE with the flag: clear it
    assert(CoaRulesetPvpFlagChange(false, true, false) == 0);
    assert(CoaRulesetPvpFlagChange(false, false, true) == 0);   // no ruleset aura yet: leave it to the core
    return 0;
}
