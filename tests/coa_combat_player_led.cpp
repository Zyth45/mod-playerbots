// "The player leads" (src/Ai/Coa/Combat/PlayerLed/CoaPlayerLedRule.h).
#include "Ai/Coa/Combat/PlayerLed/CoaPlayerLedRule.h"

#include <cassert>
#include <cstdio>

using namespace CoaPlayerLedRule;

int main()
{
    // Main tank: off, the stock order (raid frame, then the first tank), whatever the bar says.
    assert(ChooseMainTank(false, true, true, true) == MainTankSource::RaidFrame);
    assert(ChooseMainTank(false, true, false, true) == MainTankSource::FirstTank);
    // On: the player's bar first; a dead bar player hands over to the stock order.
    assert(ChooseMainTank(true, true, true, true) == MainTankSource::PlayerBar);
    assert(ChooseMainTank(true, false, true, true) == MainTankSource::RaidFrame);
    assert(ChooseMainTank(true, false, false, false) == MainTankSource::None);

    // Boss stand-down: Dungeon Clear's roster inside its runs; outside, the local flags once PlayerLed is on.
    assert(UseRosterProvider(true, true, true));
    assert(UseRosterProvider(true, false, false));   // off: unchanged
    assert(!UseRosterProvider(true, false, true));   // a player leads: the local fallback
    assert(!UseRosterProvider(false, true, true));   // no provider at all

    // Stuck-combat breaker: Dungeon Clear's hook in its runs, the module key with a player tank or outside a run.
    assert(StuckCombatAllowed(true, false, true, false));
    assert(!StuckCombatAllowed(true, false, false, true));
    assert(!StuckCombatAllowed(true, true, true, false));  // player tank, key 0: never, as before
    assert(StuckCombatAllowed(true, true, false, true));   // player tank, key 1: the module decides
    assert(StuckCombatAllowed(false, false, false, true));

    std::puts("coa_combat_player_led: ok");
    return 0;
}
