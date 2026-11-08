// The bots' manners at a loot roll (src/Ai/Coa/Combat/Gear/CoaRollRule.h).
#include "Ai/Coa/Combat/Gear/CoaRollRule.h"

#include <cassert>

using CoaRollRule::Polite;
using CoaRollRule::Vote;

int main()
{
    // The three cases the user's rule names, for a token or a piece the gear score wants (Need):
    // 1. LootNeedRollLevel 1 (the default): Greed.
    assert(Polite(Vote::Need, 1, false, false, true) == Vote::Greed);
    // 2. LootNeedRollLevel 2 ("Bots need items"): Need.
    assert(Polite(Vote::Need, 2, false, false, true) == Vote::Need);
    // 3. Level 2, but a real player rolled Need on it: Greed, never against the player.
    assert(Polite(Vote::Need, 2, false, true, true) == Vote::Greed);

    // Level 0 and a unique item already owned: Pass.
    assert(Polite(Vote::Need, 0, false, false, true) == Vote::Pass);
    assert(Polite(Vote::Need, 2, true, false, true) == Vote::Pass);
    // A Greed with greed rolls off is a Pass, the same as one demoted from Need.
    assert(Polite(Vote::Greed, 2, false, false, false) == Vote::Pass);
    assert(Polite(Vote::Need, 1, false, false, false) == Vote::Pass);
    // Pass stays Pass.
    assert(Polite(Vote::Pass, 2, false, false, true) == Vote::Pass);
    return 0;
}