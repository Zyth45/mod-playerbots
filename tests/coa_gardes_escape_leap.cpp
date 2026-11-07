// The escape leap ban (src/Ai/Coa/Gardes/EscapeLeap): Dungeon Clear's t/TestEscapeLeap.cpp as asserts, then the
// NON-REGRESSION half against Dungeon Clear 2.9.4 (tests/dc294) with a bot tank driving an active run.
#include "Ai/Coa/Gardes/EscapeLeap/CoaEscapeLeap.h"

#include "dc294/Dc294Glue.h"

#include <cassert>

int main()
{
    // tr-20260926-151320-21: a mage blinked and a hunter disengaged out of a camp fight into Karazhan's banquet hall.
    assert(CoaEscapeLeap::IsBanned("blink"));       // CastBlinkBackAction's getName()
    assert(CoaEscapeLeap::IsBanned("blink back"));  // its registry key
    assert(CoaEscapeLeap::IsBanned("disengage"));
    assert(!CoaEscapeLeap::IsBanned("frost nova"));
    assert(!CoaEscapeLeap::IsBanned("frostbolt"));
    assert(!CoaEscapeLeap::IsBanned("feign death"));
    assert(!CoaEscapeLeap::IsBanned("flee"));
    assert(!CoaEscapeLeap::IsBanned(""));

    // NON-REGRESSION: every action name either side knows, every gate fact. In a Dungeon Clear run the bot is in an
    // instance and its reference tank is the run leader (Dungeon Clear's PartyTank).
    char const* const names[] = {"blink", "blink back", "disengage", "frost nova", "frostbolt", "feign death",
                                 "flee", "runaway", "charge", "", "Blink", "disengage "};
    for (char const* name : names)
    {
        assert(CoaEscapeLeap::IsBanned(name) == DcEscapeLeap::IsBanned(name));
        for (int partyTank = 0; partyTank < 2; ++partyTank)
            for (int standDown = 0; standDown < 2; ++standDown)
            {
                CoaEscapeLeap::Gate g;
                g.hasTank = partyTank;
                g.inInstance = true;
                g.raidStandDown = standDown;
                g.combatEngine = true;
                assert(CoaEscapeLeap::Bans(CoaEscapeLeap::IsBanned(name), g) ==
                       Dc294Glue::EscapeLeapCombat(name, partyTank, standDown));
                g.combatEngine = false;
                assert(CoaEscapeLeap::Bans(CoaEscapeLeap::IsBanned(name), g) ==
                       Dc294Glue::EscapeLeapNonCombat(name, partyTank));
            }
    }

    // New outside a run: any group with a reference tank (a human Main Tank too), but only in a dungeon or raid.
    CoaEscapeLeap::Gate g;
    g.hasTank = true;
    g.inInstance = false;
    assert(!CoaEscapeLeap::Bans(true, g));
    g.inInstance = true;
    assert(CoaEscapeLeap::Bans(true, g));
    g.hasTank = false;
    assert(!CoaEscapeLeap::Bans(true, g));
}
