#include "CoaBotRacePolicy.h"
#include "CoaBotRaceRules.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using namespace CoaBotRace;

    // No line: exactly the 3.1.3 behavior, race by race.
    Profile const none;
    std::vector<std::vector<std::uint32_t>> const lists = {{}, {32, 72, 77, 97}, {1}, {55, 63, 97, 32, 72, 77}};
    for (std::uint32_t race = 0; race <= MAX_RACE; ++race)
    {
        for (auto const& excluded : lists)
            for (bool custom : {false, true})
                assert(Allowed(race, none, custom, excluded) == IsCoaBotRaceAllowed(race, custom, excluded));

        // The 3.1.3 gender lists, as they were written in RandomPlayerbotFactory.cpp.
        bool const maleOnly = race == 15 || race == 17 || race == 18 || (race >= 22 && race <= 26) || race == 32 ||
                              race == 50 || race == 53 || race == 68 || race == 69 || race == 83 || race == 97;
        bool const femaleOnly = race == 57;
        Gender const g = ResolveGender(none, race, false);
        assert(g == (femaleOnly ? Gender::Female : maleOnly ? Gender::Male : Gender::Any));

        assert(IsAllianceWith(none, race, true) && !IsAllianceWith(none, race, false));
        for (std::uint8_t genType = 0; genType < 4; ++genType)
            assert(KeepSection(none.look, genType, 3));
        for (std::uint8_t cls = 1; cls < 32; ++cls)
            assert(ClassAllowed(none, cls));
    }
    // Base races keep the core's Alliance mask.
    for (std::uint32_t race = 1; race <= 11; ++race)
        assert(BaseRaceIsAlliance(race) == bool((1u << (race - 1)) & 0x44D));

    // A full line for an added race.
    Profile p;
    std::string error;
    assert(Parse("bots=1 gender=female side=horde look=simple names=orc classes=1,2,5", p, error));
    assert(p.bots == 1 && p.gender == Gender::Female && p.side == Side::Horde && p.look == Look::Simple);
    assert(p.namesRace == 2 && p.classes == (std::vector<std::uint8_t>{1, 2, 5}));
    assert(Allowed(85, p, false, {}) && !Allowed(85, p, false, {85}));
    assert(ResolveGender(p, 85, true) == Gender::Female);
    assert(!IsAllianceWith(p, 85, true));
    assert(ClassAllowed(p, 5) && !ClassAllowed(p, 3));
    assert(KeepSection(p.look, 0, 4) && KeepSection(p.look, 1, 0) && !KeepSection(p.look, 1, 1));
    assert(!KeepSection(p.look, 2, 0) && KeepSection(p.look, 3, 0) && !KeepSection(p.look, 3, 2));

    // Separators and case; bots=0 closes a race, even a base one; a base race keeps its side.
    assert(Parse(" Bots=0 ; SIDE=Alliance ", p, error) && p.bots == 0 && p.side == Side::Alliance);
    assert(!Allowed(1, p, true, {}));
    assert(!IsAllianceWith(p, 2, false) && IsAllianceWith(p, 96, false));
    assert(Parse("", p, error) && p.bots == -1);

    // The core's male-only races count only where neither the line nor the module's list decides.
    assert(ResolveGender(none, 98, true) == Gender::Male);
    assert(ResolveGender(none, 57, true) == Gender::Female);
    assert(Parse("gender=any", p, error) && ResolveGender(p, 98, true) == Gender::Any);
    assert(ResolveGender(p, 53, false) == Gender::Any);

    // A typo drops the whole line.
    for (char const* bad : {"bots=2", "gender=man", "side=neutral", "look=pretty", "names=murloc", "classes=0",
                            "classes=1,,2", "classes=40", "colour=red", "bots", "=1", "bots="})
    {
        Profile kept;
        kept.bots = 1;
        error.clear();
        assert(!Parse(bad, kept, error) && !error.empty() && kept.bots == 1);
    }

    // Keys.
    std::string const prefix = "AiPlayerbot.BotRace.";
    assert(RaceOfKey("AiPlayerbot.BotRace.85", prefix) == 85);
    assert(RaceOfKey("AiPlayerbot.BotRace.255", prefix) == 255);
    assert(RaceOfKey("AiPlayerbot.BotRace.256", prefix) == 0);
    assert(RaceOfKey("AiPlayerbot.BotRace.0", prefix) == 0);
    assert(RaceOfKey("AiPlayerbot.BotRace.x1", prefix) == 0);
    assert(RaceOfKey("AiPlayerbot.BotRace.", prefix) == 0);
    assert(RaceOfKey("AiPlayerbot.BotRaces", prefix) == 0);

    // One id per side, as in the custom races' list ("20/29").
    using Ids = std::vector<std::uint32_t>;
    assert(RacesOfKey("AiPlayerbot.BotRace.85", prefix) == Ids{85});
    assert(RacesOfKey("AiPlayerbot.BotRace.20/29", prefix) == (Ids{20, 29}));
    assert(RacesOfKey("AiPlayerbot.BotRace.54/58/54", prefix) == (Ids{54, 58}));
    assert(RacesOfKey("AiPlayerbot.BotRace.20/", prefix).empty());
    assert(RacesOfKey("AiPlayerbot.BotRace./29", prefix).empty());
    assert(RacesOfKey("AiPlayerbot.BotRace.20/300", prefix).empty());
    assert(RacesOfKey("AiPlayerbot.BotRace.20,29", prefix).empty());
    assert(RacesOfKey("AiPlayerbot.BotRace.1/2/3/4/5/6/7/8/10", prefix).empty());
    assert(RacesOfKey("AiPlayerbot.BotRaces", prefix).empty());
    return 0;
}
