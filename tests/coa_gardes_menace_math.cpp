#include "Ai/Coa/Gardes/Menace/CoaThreatLeadMath.h"

#include <cassert>

using namespace CoaThreatLeadMath;

namespace
{
void HoldThreatLead()
{
    // Tank hit at 1000, lead 1500: held until 2500.
    assert(ShouldHoldThreatLead(false, false, 900, 1000, 1000, 1500, NoHitCapMs, 100.0f, 60.0f));
    assert(ShouldHoldThreatLead(false, false, 900, 1000, 2499, 1500, NoHitCapMs, 100.0f, 60.0f));
    assert(!ShouldHoldThreatLead(false, false, 900, 1000, 2500, 1500, NoHitCapMs, 100.0f, 60.0f));
    // A hit stamped a tick ahead of now still holds.
    assert(ShouldHoldThreatLead(false, false, 900, 1001, 1000, 1500, NoHitCapMs, 100.0f, 60.0f));
    // No hit yet: lead + cap from the combat flag.
    assert(ShouldHoldThreatLead(false, false, 1000, 0, 4999, 1500, NoHitCapMs, 100.0f, 60.0f));
    assert(!ShouldHoldThreatLead(false, false, 1000, 0, 5000, 1500, NoHitCapMs, 100.0f, 60.0f));
    // Exemptions: a tank, a member under attack, the lead off, no fight, a tank under the panic health.
    assert(!ShouldHoldThreatLead(true, false, 900, 1000, 1100, 1500, NoHitCapMs, 100.0f, 60.0f));
    assert(!ShouldHoldThreatLead(false, true, 900, 1000, 1100, 1500, NoHitCapMs, 100.0f, 60.0f));
    assert(!ShouldHoldThreatLead(false, false, 900, 1000, 1100, 0, NoHitCapMs, 100.0f, 60.0f));
    assert(!ShouldHoldThreatLead(false, false, 0, 0, 1100, 1500, NoHitCapMs, 100.0f, 60.0f));
    assert(!ShouldHoldThreatLead(false, false, 900, 1000, 1100, 1500, NoHitCapMs, 59.0f, 60.0f));
    // Panic 0 switches that bypass off.
    assert(ShouldHoldThreatLead(false, false, 900, 1000, 1100, 1500, NoHitCapMs, 5.0f, 0.0f));
}

void ReleaseFollower()
{
    assert(ShouldReleaseFollower(true, false, 1000, 1000, 1500, 100.0f, 60.0f));
    assert(ShouldReleaseFollower(false, true, 1000, 1000, 1500, 100.0f, 60.0f));
    assert(ShouldReleaseFollower(false, false, 1000, 1000, 0, 100.0f, 60.0f));
    assert(ShouldReleaseFollower(false, false, 0, 1000, 1500, 100.0f, 60.0f));
    assert(ShouldReleaseFollower(false, false, 1000, 1000, 1500, 50.0f, 60.0f));
    assert(!ShouldReleaseFollower(false, false, 1000, 2499, 1500, 100.0f, 60.0f));
    assert(ShouldReleaseFollower(false, false, 1000, 2500, 1500, 100.0f, 60.0f));
    // A stamp ahead of now: not released.
    assert(!ShouldReleaseFollower(false, false, 1000, 999, 1500, 100.0f, 60.0f));
}

void Clock()
{
    FightClock c;
    assert(c.Empty());
    c.EnterCombat(1000);
    assert(c.combatSinceMs == 1000 && c.firstHitMs == 0);
    c.Hit(1200);
    assert(c.CurrentFirstHit() == 1200);
    c.Hit(1500);  // the first hit stays
    assert(c.CurrentFirstHit() == 1200);
    // A new fight: the old first hit is gone.
    c.EnterCombat(3000);
    assert(c.CurrentFirstHit() == 0);
    c.Hit(3100);
    assert(c.CurrentFirstHit() == 3100);
    // A missed enter-combat edge: the hit starts the fight.
    FightClock m;
    m.Hit(500);
    assert(m.combatSinceMs == 500 && m.CurrentFirstHit() == 500);
    // A stale hit older than the fight reads as none.
    FightClock s;
    s.combatSinceMs = 2000;
    s.firstHitMs = 1000;
    assert(s.CurrentFirstHit() == 0);
    s.Hit(2100);
    assert(s.CurrentFirstHit() == 2100);
    // Never 0 for a real stamp.
    FightClock z;
    z.EnterCombat(0);
    assert(z.combatSinceMs == 1);
    z.LeaveCombat();
    assert(z.Empty());
}

void Latch()
{
    std::uint32_t latch = 0;
    assert(!EngagedLatched(false, 100, 3000, latch));
    assert(EngagedLatched(true, 1000, 3000, latch) && latch == 1000);
    assert(EngagedLatched(false, 3999, 3000, latch));
    assert(!EngagedLatched(false, 4000, 3000, latch) && latch == 0);
    // No latch: the bare read.
    std::uint32_t none = 0;
    assert(EngagedLatched(true, 10, 0, none) && none == 0);
    assert(!EngagedLatched(false, 11, 0, none));
}

void Members()
{
    assert(MemberAssists(false, true));
    assert(MemberAssists(true, false));
    assert(!MemberAssists(true, true));
    // Melee inside its range; ranged inside spell range + reaches (30 + 3).
    assert(IsWithinAssistAttackRange(true, 5.0f, 6.5f, 30.0f, 3.0f));
    assert(!IsWithinAssistAttackRange(true, 7.0f, 6.5f, 30.0f, 3.0f));
    assert(IsWithinAssistAttackRange(false, 32.9f, 6.5f, 30.0f, 3.0f));
    assert(!IsWithinAssistAttackRange(false, 33.1f, 6.5f, 30.0f, 3.0f));
}

void GiveUp()
{
    std::uint32_t since = 0;
    assert(!AssistCloseGivenUp(false, true, 1000, 5000, since) && since == 1000);
    assert(!AssistCloseGivenUp(false, true, 5999, 5000, since));
    assert(AssistCloseGivenUp(false, true, 6000, 5000, since));
    // A move resets the clock; a new target restarts it.
    assert(!AssistCloseGivenUp(true, true, 6100, 5000, since) && since == 0);
    assert(!AssistCloseGivenUp(false, true, 7000, 5000, since) && since == 7000);
    assert(!AssistCloseGivenUp(false, false, 13000, 5000, since) && since == 13000);
}

void Detour()
{
    // Close: the slack wins (10 + 20 = 30); far: the ratio wins (40 * 2 = 80).
    assert(WithinDetour(10.0f, 30.0f));
    assert(!WithinDetour(10.0f, 30.5f));
    assert(WithinDetour(40.0f, 80.0f));
    assert(!WithinDetour(40.0f, 81.0f));
}

void Tiers()
{
    // PullPlayerReleaseDelay: 1.5, heroic 3.0, no raid layer.
    assert(Tiered(1.5f, 3.0f, 0.0f, true, false, false, false) == 1.5f);
    assert(Tiered(1.5f, 3.0f, 0.0f, true, false, true, false) == 3.0f);
    assert(Tiered(1.5f, 3.0f, 0.0f, true, false, true, true) == 3.0f);
    // PartyMaxSpread: 25, raid 40, no heroic layer.
    assert(Tiered(25.0f, 0.0f, 40.0f, false, true, true, false) == 25.0f);
    assert(Tiered(25.0f, 0.0f, 40.0f, false, true, false, true) == 40.0f);
}
}

int main()
{
    HoldThreatLead();
    ReleaseFollower();
    Clock();
    Latch();
    Members();
    GiveUp();
    Detour();
    Tiers();
}
