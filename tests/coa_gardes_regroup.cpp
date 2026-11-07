// The combat regroup (src/Ai/Coa/Gardes/Regroup): Dungeon Clear's gtest cases (t/TestCombatRegroup.cpp, the standoff
// and route cases of t/TestHealReposition.cpp, the anchor cases of t/TestDungeonClearMath.cpp) as asserts, then the
// NON-REGRESSION half: with a bot tank driving an active run, the module gives Dungeon Clear 2.9.4's verdicts
// (tests/dc294, the 2.9.4 code copied verbatim / transcribed) tick for tick.
#include "Ai/Coa/Gardes/Common/CoaGardesGate.h"
#include "Ai/Coa/Gardes/Regroup/CoaRegroupDecision.h"
#include "Ai/Coa/Gardes/Regroup/CoaStandoffMath.h"

#include "dc294/Dc294Glue.h"

#include <cassert>
#include <cmath>
#include <vector>

using CoaRegroupDecision::DecideCombatRegroup;
using CoaRegroupDecision::RegroupInputs;
using CoaRegroupDecision::RegroupVerdict;

namespace
{
    bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

    RegroupInputs BaseDps()
    {
        RegroupInputs in;
        in.isHealer = false;
        in.isMelee = true;
        in.hasVisibleAttacker = true;
        in.tankLos = true;
        in.tankDist2d = 10.0f;
        in.healRange = 30.0f;
        in.hardTether = 40.0f;
        in.slack = 8.0f;
        return in;
    }

    RegroupInputs BaseHealer()
    {
        RegroupInputs in = BaseDps();
        in.isHealer = true;
        in.isMelee = false;
        in.hasVisibleAttacker = false;
        return in;
    }

    void KernelCases()
    {
        RegroupInputs in = BaseDps();
        // Casting / CC'd: None, even past the tether.
        in.hasVisibleAttacker = false;
        in.casting = true;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in.casting = false;
        in.ccd = true;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in = BaseDps();
        in.casting = true;
        in.tankDist2d = 100.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in = BaseHealer();
        in.ccd = true;
        in.tankDist2d = 100.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);

        // The hard tether beats the DPS and healer clauses; equal is not past it.
        in = BaseDps();
        in.tankDist2d = 41.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::HardTether);
        in = BaseHealer();
        in.hasHurtHealTarget = true;
        in.tankDist2d = 55.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::HardTether);
        in = BaseDps();
        in.tankDist2d = 40.0f;
        assert(DecideCombatRegroup(in) != RegroupVerdict::HardTether);

        // DPS: attacker visibility.
        assert(DecideCombatRegroup(BaseDps()) == RegroupVerdict::None);
        in = BaseDps();
        in.hasVisibleAttacker = false;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
        in.isMelee = false;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
        in = BaseDps();
        in.tankDist2d = 30.0f;  // a melee chasing a runner keeps its attacker
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in = BaseDps();
        in.hasHurtHealTarget = true;
        in.tankLos = false;
        in.tankDist2d = 39.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);

        // Healer: the heal reposition owns a hurt target; pre-positioning otherwise.
        in = BaseHealer();
        in.hasHurtHealTarget = true;
        in.tankLos = false;
        in.tankDist2d = 39.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in = BaseHealer();
        in.tankLos = false;
        in.tankDist2d = 10.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
        in = BaseHealer();
        in.tankDist2d = 25.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
        in.tankDist2d = 15.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in.tankDist2d = 21.9f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in.tankDist2d = 22.1f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
        in.slack = 0.0f;
        in.tankDist2d = 29.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in.tankDist2d = 31.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);

        // DPS: an in-LOS target is contribution too (the attacker list is threat-derived).
        in = BaseDps();
        in.hasVisibleAttacker = false;
        in.hasLosTarget = true;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in.isMelee = false;
        assert(DecideCombatRegroup(in) == RegroupVerdict::None);
        in.hasLosTarget = false;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
        in = BaseDps();
        in.hasVisibleAttacker = false;
        in.hasLosTarget = true;
        in.tankDist2d = 41.0f;
        assert(DecideCombatRegroup(in) == RegroupVerdict::HardTether);
        in = BaseHealer();
        in.hasLosTarget = true;
        in.tankLos = false;
        assert(DecideCombatRegroup(in) == RegroupVerdict::Reconnect);
    }

    void StandoffCases()
    {
        using CoaStandoffMath::Point;
        // Candidate count is ringPoints + 1, all on the circle, the first on the bot's side.
        std::vector<Point> pts = CoaStandoffMath::StandoffCandidates({100, 100, 50}, {80, 100, 50}, 10.0f, 7);
        assert(pts.size() == 8u);
        pts = CoaStandoffMath::StandoffCandidates({100, 100, 50}, {60, 130, 50}, 12.0f, 7);
        for (Point const& p : pts)
            assert(Near(std::hypot(p.x - 100.0f, p.y - 100.0f), 12.0f, 1e-2f));
        pts = CoaStandoffMath::StandoffCandidates({100, 100, 50}, {70, 100, 50}, 10.0f, 7);
        assert(Near(pts[0].x, 90.0f, 1e-2f) && Near(pts[0].y, 100.0f, 1e-2f));
        float const first = std::hypot(pts[0].x - 70.0f, pts[0].y - 100.0f);
        for (std::size_t i = 1; i < pts.size(); ++i)
            assert(first <= std::hypot(pts[i].x - 70.0f, pts[i].y - 100.0f) + 1e-3f);
        pts = CoaStandoffMath::StandoffCandidates({100, 100, 50}, {100, 100, 50}, 10.0f, 7);
        assert(pts.size() == 8u && Near(pts[0].x, 110.0f, 1e-2f) && Near(pts[0].y, 100.0f, 1e-2f));

        // The close-on fallback walks the ROUTE, not the chord (Karazhan's Maiden hairpin).
        Point p = CoaStandoffMath::PointShortOfPathEnd({{0, 0, 92}, {0, 30, 92}, {10, 30, 92}, {10, 0, 92}}, 5.0f);
        assert(Near(p.x, 10.0f, 1e-3f) && Near(p.y, 5.0f, 1e-3f) && Near(p.z, 92.0f, 1e-3f));
        p = CoaStandoffMath::PointShortOfPathEnd({{0, 0, 0}, {0, 20, 0}, {10, 20, 0}}, 12.0f);
        assert(Near(p.x, 0.0f, 1e-3f) && Near(p.y, 18.0f, 1e-3f));
        p = CoaStandoffMath::PointShortOfPathEnd({{0, 0, 424.5f}, {0, 20, 424.5f}, {0, 40, 449.3f}}, 5.0f);
        assert(p.z > 424.5f && p.z < 449.3f && p.y > 20.0f);
        p = CoaStandoffMath::PointShortOfPathEnd({{1, 2, 3}, {1, 5, 3}}, 5.0f);
        assert(Near(p.y, 2.0f, 1e-3f));
        p = CoaStandoffMath::PointShortOfPathEnd({{4, 4, 4}, {4, 4, 4}}, 0.0f);
        assert(!std::isnan(p.x) && Near(p.z, 4.0f, 1e-3f));
        p = CoaStandoffMath::PointShortOfPathEnd({}, 5.0f);
        assert(!std::isnan(p.x));

        // The fight anchor: never a stranded far holder, never an unreachable level.
        assert(!CoaStandoffMath::IsRegroupAnchorCandidate(141.8f, 100.0f, true));
        assert(!CoaStandoffMath::IsRegroupAnchorCandidate(40.0f, 100.0f, false));
        assert(CoaStandoffMath::IsRegroupAnchorCandidate(25.0f, 100.0f, true));
        assert(CoaStandoffMath::IsRegroupAnchorCandidate(100.0f, 100.0f, true));
    }

    DcRegroupDecision::RegroupInputs ToDc(RegroupInputs const& m)
    {
        DcRegroupDecision::RegroupInputs d;
        d.isHealer = m.isHealer;
        d.isMelee = m.isMelee;
        d.casting = m.casting;
        d.ccd = m.ccd;
        d.hasVisibleAttacker = m.hasVisibleAttacker;
        d.hasLosTarget = m.hasLosTarget;
        d.hasHurtHealTarget = m.hasHurtHealTarget;
        d.tankLos = m.tankLos;
        d.tankDist2d = m.tankDist2d;
        d.healRange = m.healRange;
        d.hardTether = m.hardTether;
        d.slack = m.slack;
        return d;
    }

    // Pseudo-random but reproducible.
    struct Lcg
    {
        uint32_t s = 12345u;
        uint32_t Next() { return s = s * 1664525u + 1013904223u; }
        bool Bit() { return (Next() >> 16) & 1u; }
        float Range(float lo, float hi) { return lo + (hi - lo) * float((Next() >> 8) & 0xFFFF) / 65535.0f; }
    };

    RegroupInputs RandomInputs(Lcg& r)
    {
        RegroupInputs in;
        in.isHealer = r.Bit();
        in.isMelee = r.Bit();
        in.casting = (r.Next() % 6) == 0;
        in.ccd = (r.Next() % 8) == 0;
        in.hasVisibleAttacker = r.Bit();
        in.hasLosTarget = r.Bit();
        in.hasHurtHealTarget = r.Bit();
        in.tankLos = r.Bit();
        in.tankDist2d = r.Range(0.0f, 60.0f);
        in.healRange = r.Range(25.0f, 40.0f);
        in.hardTether = r.Range(15.0f, 100.0f);
        in.slack = r.Range(0.0f, 20.0f);
        return in;
    }

    // NON-REGRESSION: the verdict on a dense sample, every field crossed.
    void VerdictMatchesDc294()
    {
        Lcg r;
        for (int i = 0; i < 200000; ++i)
        {
            RegroupInputs const in = RandomInputs(r);
            assert(static_cast<int>(DecideCombatRegroup(in)) ==
                   static_cast<int>(DcRegroupDecision::DecideCombatRegroup(ToDc(in))));
        }
    }

    // NON-REGRESSION: the whole trigger (gate + verdict + debounce / latch / cooldown) over long random fights, with a
    // bot tank driving the run: the module's reference tank is the run leader, a bot (never human).
    void TriggerMatchesDc294WithABotTankDriving()
    {
        Lcg r;
        for (int fight = 0; fight < 2000; ++fight)
        {
            Dc294Glue::RegroupTrigger dc;
            CoaRegroupDecision::RegroupLatch latch;
            uint32_t now = 1000u + (r.Next() % 100000u);
            bool const botIsTank = r.Bit();
            for (int tick = 0; tick < 300; ++tick)
            {
                now += 100u + (r.Next() % 400u);
                Dc294Glue::RegroupFacts f;
                f.botDead = (r.Next() % 50) == 0;
                f.inCombat = (r.Next() % 30) != 0;
                f.combatRegroup = (r.Next() % 40) != 0;
                f.partyTank = (r.Next() % 25) != 0;
                f.tankIsBot = (r.Next() % 30) == 0;
                f.tankSameMap = (r.Next() % 40) != 0;
                f.campHoldPassive = (r.Next() % 20) == 0;
                f.cooldownSec = r.Range(0.0f, 30.0f);
                RegroupInputs in = RandomInputs(r);
                in.tankDist2d = r.Range(0.0f, 45.0f);
                in.hardTether = 40.0f;
                f.in = ToDc(in);

                bool const dcFires = dc.IsActive(f, now);

                // The module, exactly as CoaCombatRegroupTrigger::IsActive composes it.
                CoaRegroupDecision::GateInputs g;
                g.botAlive = !f.botDead;
                g.inCombat = f.inCombat;
                g.featureOn = f.combatRegroup;
                g.hasTank = f.partyTank;
                g.tankIsSelf = f.tankIsBot;
                g.tankSameMap = f.tankSameMap;
                g.tankIsHuman = false;
                g.botIsTank = botIsTank;
                bool coaFires = false;
                if (!CoaRegroupDecision::GateOpen(g) || f.campHoldPassive)
                    CoaRegroupDecision::StandDown(latch);
                else
                    coaFires = CoaRegroupDecision::Step(latch, DecideCombatRegroup(in), now,
                                                        uint32_t(f.cooldownSec * 1000.0f));
                assert(coaFires == dcFires);
                assert(latch.latched == dc._latched && latch.pendingSince == dc._pendingSince &&
                       latch.cooldownUntil == dc._cooldownUntil);
            }
        }
    }

    // Player-led (outside a run): a bot tank under a HUMAN reference tank never regroups; anyone else still does.
    void HumanMainTankLeavesTheBotTanksAlone()
    {
        CoaRegroupDecision::GateInputs g;
        g.inCombat = true;
        g.hasTank = true;
        g.tankIsHuman = true;
        g.botIsTank = true;
        assert(!CoaRegroupDecision::GateOpen(g));
        g.botIsTank = false;
        assert(CoaRegroupDecision::GateOpen(g));
        g.tankIsHuman = false;
        g.botIsTank = true;
        assert(CoaRegroupDecision::GateOpen(g));
        g.hasTank = false;  // a dead reference tank is no reference (CoaGroupTank drops it)
        assert(!CoaRegroupDecision::GateOpen(g));
        assert(!CoaGardesGate::MemberRungApplies(true, true) && CoaGardesGate::MemberRungApplies(false, true));
    }
}

int main()
{
    KernelCases();
    StandoffCases();
    VerdictMatchesDc294();
    TriggerMatchesDc294WithABotTankDriving();
    HumanMainTankLeavesTheBotTanksAlone();
}
