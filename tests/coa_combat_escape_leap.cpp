// The escape leap ban of the CoA Combat guards (src/Ai/Coa/Combat/EscapeLeap), from its behaviour spec.
#include "Ai/Coa/Combat/EscapeLeap/CoaEscapeLeapRule.h"

#include <cassert>

using CoaEscapeLeapRule::Forbids;
using CoaEscapeLeapRule::IsEscapeLeap;
using CoaEscapeLeapRule::Situation;

int main()
{
    // Only the mage's Blink (both of its names) and the hunter's Disengage, matched exactly.
    assert(IsEscapeLeap("blink"));
    assert(IsEscapeLeap("blink back"));
    assert(IsEscapeLeap("disengage"));
    for (char const* other : {"", "Blink", "disengage ", "frost nova", "frostbolt", "feign death", "flee", "runaway",
                              "charge", "blink strike"})
        assert(!IsEscapeLeap(other));

    // Every combination of the four facts.
    for (int tank = 0; tank < 2; ++tank)
        for (int dungeon = 0; dungeon < 2; ++dungeon)
            for (int combat = 0; combat < 2; ++combat)
                for (int encounter = 0; encounter < 2; ++encounter)
                {
                    Situation s;
                    s.groupHasTank = tank;
                    s.inDungeonOrRaid = dungeon;
                    s.combatEngine = combat;
                    s.raidEncounterInProgress = encounter;
                    bool const expected = tank && dungeon && !(combat && encounter);
                    assert(Forbids(s) == expected);
                }

    // The cases the spec names.
    Situation s;
    s.groupHasTank = true;
    s.inDungeonOrRaid = false;  // open world: the leaps stay
    assert(!Forbids(s));
    s.inDungeonOrRaid = true;  // dungeon group behind a tank
    assert(Forbids(s));
    s.combatEngine = true;
    assert(Forbids(s));
    s.raidEncounterInProgress = true;  // raid boss fight: the raid strategy decides
    assert(!Forbids(s));
    s.combatEngine = false;  // ...but out of combat the ban holds
    assert(Forbids(s));
    s.groupHasTank = false;  // no tank to fight behind
    assert(!Forbids(s));
    return 0;
}
