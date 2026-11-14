// Bloodlust and Heroism kept for bosses (src/Ai/Coa/Combat/Lust/CoaBossLustRule.h).
#include "Ai/Coa/Combat/Lust/CoaBossLustRule.h"

#include <cassert>
#include <cstring>

using namespace CoaBossLustRule;

namespace
{
    Situation Grouped(bool boss, float health, uint32_t threshold = 30)
    {
        Situation s;
        s.enabled = true;
        s.grouped = true;
        s.bossInFight = boss;
        s.bossHealthPct = health;
        s.thresholdPct = threshold;
        return s;
    }
}

int main()
{
    // Off, alone, or where a raid strategy times it: the stock logic decides.
    Situation off = Grouped(false, 100.0f);
    off.enabled = false;
    assert(Decide(off) == Verdict::Stock);
    Situation alone = Grouped(false, 100.0f);
    alone.grouped = false;
    assert(Decide(alone) == Verdict::Stock);
    Situation kara = Grouped(true, 60.0f);
    kara.encounterDecides = true;
    assert(Decide(kara) == Verdict::Stock);

    // Trash: never.
    assert(Decide(Grouped(false, 100.0f)) == Verdict::Forbid);
    assert(Decide(Grouped(false, 10.0f)) == Verdict::Forbid);

    // A boss: at the pull, then under the threshold, not in between.
    assert(Decide(Grouped(true, 100.0f)) == Verdict::Allow);
    assert(Decide(Grouped(true, 90.0f)) == Verdict::Allow);
    assert(Decide(Grouped(true, 89.9f)) == Verdict::Forbid);
    assert(Decide(Grouped(true, 31.0f)) == Verdict::Forbid);
    assert(Decide(Grouped(true, 30.0f)) == Verdict::Allow);
    assert(Decide(Grouped(true, 5.0f)) == Verdict::Allow);

    // Threshold 0: at the pull only.
    assert(Decide(Grouped(true, 95.0f, 0)) == Verdict::Allow);
    assert(Decide(Grouped(true, 5.0f, 0)) == Verdict::Forbid);

    // Baron Geddon: under 10 % only, not at the pull.
    assert(LateBurstPct("Baron Geddon") == 10u && LateBurstPct("Ragnaros") == 0u);
    Situation geddon = Grouped(true, 100.0f);
    geddon.lateBurstPct = LateBurstPct("Baron Geddon");
    assert(Decide(geddon) == Verdict::Forbid);
    geddon.bossHealthPct = 25.0f;
    assert(Decide(geddon) == Verdict::Forbid);
    geddon.bossHealthPct = 9.5f;
    assert(Decide(geddon) == Verdict::Allow);

    // Group bursts read from the spell data: Tempest's Call triggers Worn Out, War Cry checks Exhaustion 572126, a
    // spell that only triggers a damage spell is none.
    uint32_t const tempest[3] = {0, 0, 804457};
    uint32_t const warCry[3] = {0, 0, 0};
    uint32_t const fireball[3] = {133, 0, 0};
    assert(IsGroupBurst(804457, tempest) && IsGroupBurst(0, tempest));
    assert(IsGroupBurst(572126, warCry));
    assert(!IsGroupBurst(0, warCry) && !IsGroupBurst(0, fireball));
    assert(IsBurstDebuff(1157724) && !IsBurstDebuff(2825));

    // The encounter strategies the rule steps aside for.
    int count = 0;
    bool ssc = false;
    for (char const* const* name = EncounterStrategies(); *name; ++name, ++count)
        ssc = ssc || !std::strcmp(*name, "ssc");
    assert(count == 7 && ssc);
    return 0;
}
