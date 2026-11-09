// The bots' manners at a loot roll (src/Ai/Coa/Combat/Gear/CoaRollRule.h).
#include "Ai/Coa/Combat/Gear/CoaRollRule.h"

#include <cassert>

using CoaRollRule::HoldVote;
using CoaRollRule::KeepNeed;
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
    // The held vote: only at level 2, only for a Need, only while a real player may still vote, at most the wait.
    assert(HoldVote(Vote::Need, 2, true, 0, 20000));
    assert(HoldVote(Vote::Need, 2, true, 19999, 20000));
    assert(!HoldVote(Vote::Need, 2, true, 20000, 20000));   // the wait is over: vote
    assert(!HoldVote(Vote::Need, 2, false, 0, 20000));      // every player voted (or none rolls): vote at once
    assert(!HoldVote(Vote::Need, 1, true, 0, 20000));       // level 1: Greed at once, as before
    assert(!HoldVote(Vote::Need, 0, true, 0, 20000));
    assert(!HoldVote(Vote::Greed, 2, true, 0, 20000));      // a Greed never waits
    assert(!HoldVote(Vote::Need, 2, true, 0, 0));           // 0: no wait
    // Capped under the roll's 60 s: a 90 s setting still votes at 50 s.
    assert(HoldVote(Vote::Need, 2, true, 49999, 90000));
    assert(!HoldVote(Vote::Need, 2, true, 50000, 90000));
    // Loot Need refined: both settings off keep every Need.
    assert(KeepNeed(false, true, 1.0f, false, false, 0.0f));
    // Epic only: a blue loses its Need, a purple keeps it.
    assert(!KeepNeed(false, true, 50.0f, false, true, 0.0f));
    assert(KeepNeed(true, true, 50.0f, false, true, 0.0f));
    // Threshold 10 %: a 4 % upgrade is a Greed, a 12 % one a Need, an empty slot always a Need.
    assert(!KeepNeed(true, true, 4.0f, false, false, 10.0f));
    assert(KeepNeed(true, true, 12.0f, false, false, 10.0f));
    assert(KeepNeed(true, true, 0.0f, true, false, 10.0f));
    // The threshold only judges gear: a token or a recipe keeps its Need.
    assert(KeepNeed(true, false, 0.0f, false, false, 10.0f));

    return 0;
}