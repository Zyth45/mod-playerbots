#include "CoaSoulRules.h"

#include <cassert>

using namespace CoaSoul;

int main()
{
    // Weights.
    assert(ParseWeights("") == DefaultWeights());
    assert((ParseWeights("\"10, 10, 10, 10, 60\"") == Weights{10, 10, 10, 10, 60}));
    assert(ParseWeights("1,2,3") == DefaultWeights());
    assert(ParseWeights("0,0,0,0,0") == DefaultWeights());
    assert(ParseWeights("a,1,1,1,1") == DefaultWeights());

    // The same soul for the same guid, archetypes in the proportions asked, traits within 15 of the profile.
    int count[ARCHETYPE_COUNT] = {};
    for (uint64_t guid = 1; guid <= 20000; ++guid)
    {
        Soul const s = Draw(guid, DefaultWeights());
        Soul const again = Draw(guid, DefaultWeights());
        assert(s.archetype == again.archetype && s.pace == again.pace && s.seed == again.seed);
        ++count[s.archetype];
        auto const p = Profile(s.archetype);
        uint8_t const traits[5] = {s.pace, s.sociability, s.curiosity, s.greed, s.aggression};
        for (int i = 0; i < 5; ++i)
            assert(int(traits[i]) >= int(p[i]) - 15 && int(traits[i]) <= int(p[i]) + 15);
    }
    assert(count[LAZY] > 3600 && count[LAZY] < 4400);
    assert(count[QUESTER] > 7500 && count[QUESTER] < 8500);
    assert(count[TRADER] > 1700 && count[TRADER] < 2300 && count[PVP] > 1700 && count[PVP] < 2300);
    // Only one archetype asked: everyone is of it.
    for (uint64_t guid = 1; guid <= 100; ++guid)
        assert(Draw(guid, Weights{0, 0, 0, 0, 5}).archetype == PVP);

    // Activities.
    assert(RpgPercent(LAZY, "rest") == 200 && RpgPercent(LAZY, "quest") == 100);
    assert(RpgPercent(TRADER, "wander_npc") == 200 && RpgPercent(PVP, "pvp") == 300);
    assert(RpgPercent(WANDERER, "flight") == 200 && RpgPercent(QUESTER, "city") == 100);
    assert(std::string(ArchetypeName(TRADER)) == "trader");
    return 0;
}
