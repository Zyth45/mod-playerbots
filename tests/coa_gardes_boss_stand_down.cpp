// Raid boss stand-down guard, moved from Dungeon Clear: hysteresis, exemption list, five-man table, group state.
#include "Ai/Coa/Gardes/BossStandDown/CoaBossStandDownKernel.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace
{
    using namespace CoaBossStandDown;

    // ---- Dungeon Clear 2.9.4, verbatim (DcBossStandDown.h Update / ClassifyAction, and IsActive's memo + fold on
    // the leader's DcRunState): the non-regression reference.
    struct DcVerdict
    {
        bool active;
        std::uint32_t lastSignalMs;
    };

    DcVerdict DcUpdate(bool wasActive, std::uint32_t lastSignalMs, bool signal, std::uint32_t nowMs)
    {
        if (signal)
            return {true, nowMs};
        if (!wasActive)
            return {false, lastSignalMs};
        if (nowMs - lastSignalMs >= 3000u)
            return {false, lastSignalMs};
        return {true, lastSignalMs};
    }

    int DcClassify(std::string const& name, bool isDcAction)  // 0 Stock, 1 Inert, 2 Defer
    {
        if (name == "dungeon clear run event combat")
            return 0;
        if (name == "dungeon clear razorgore orb" || name == "dungeon clear razorgore camp")
            return 0;
        if (name == "dungeon clear hold fire")
            return 0;
        if (name == "dungeon clear oc rider")
            return 0;
        if (name == "dungeon clear kz chess")
            return 0;
        if (name == "dungeon clear assist camp combat")
            return 0;
        if (isDcAction)
            return 1;
        return 2;
    }

    struct DcRunState
    {
        bool standDownActive = false;
        std::uint32_t standDownSignalMs = 0;
        std::uint32_t standDownEvalMs = 0;
    };

    bool DcIsActiveOnLeader(DcRunState& run, std::uint32_t now, bool signal)
    {
        if (run.standDownEvalMs && now - run.standDownEvalMs < 50u)
            return run.standDownActive;
        run.standDownEvalMs = now;
        DcVerdict const v = DcUpdate(run.standDownActive, run.standDownSignalMs, signal, now);
        run.standDownActive = v.active;
        run.standDownSignalMs = v.lastSignalMs;
        return v.active;
    }

    // A small deterministic generator for the signal sequences.
    std::uint32_t Next(std::uint32_t& seed)
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    }

    int AsInt(ActionVerdict v) { return static_cast<int>(v); }
}

int main()
{
    using namespace CoaBossStandDown;

    // ---- hysteresis (DC TestRaidCore DcBossStandDownTest) ----
    auto v = Update(false, 0, true, 5000);
    assert(v.active && v.lastSignalMs == 5000);  // enters instantly
    v = Update(false, 0, false, 5000);
    assert(!v.active);  // quiet stays inactive
    v = Update(false, 0, true, 10000);
    v = Update(v.active, v.lastSignalMs, false, 10000 + kExitGraceMs - 1);
    assert(v.active);  // holds through a gap shorter than the grace
    v = Update(v.active, v.lastSignalMs, true, 14000);
    assert(v.active && v.lastSignalMs == 14000);
    v = Update(false, 0, true, 10000);
    v = Update(v.active, v.lastSignalMs, false, 10000 + kExitGraceMs);
    assert(!v.active);  // exits after the grace
    v = Update(false, 0, true, 1000);
    v = Update(v.active, v.lastSignalMs, false, 1000 + kExitGraceMs);
    v = Update(v.active, v.lastSignalMs, true, 20000);
    assert(v.active);  // re-enters after an exit
    std::uint32_t const nearWrap = 0xFFFFFF00u;
    v = Update(false, 0, true, nearWrap);
    v = Update(v.active, v.lastSignalMs, false, nearWrap + kExitGraceMs);  // wraps
    assert(!v.active);

    // ---- exemption list (DcBossStandDownClassifyTest) ----
    auto const dc = [](char const* n) { return ClassifyAction(n, true); };
    assert(dc("dungeon clear engage trash") == ActionVerdict::Inert);
    assert(dc("dungeon clear regroup combat") == ActionVerdict::Inert);
    assert(dc("dungeon clear pull maneuver") == ActionVerdict::Inert);
    for (char const* n : {"dungeon clear run event combat", "dungeon clear razorgore orb", "dungeon clear razorgore camp",
                          "dungeon clear hold fire", "dungeon clear oc rider", "dungeon clear kz chess",
                          "dungeon clear assist camp combat"})
        assert(dc(n) == ActionVerdict::Stock);
    assert(ClassifyAction("drop target", false) == ActionVerdict::Defer);
    assert(dc("dungeon clear assist camp") == ActionVerdict::Inert);  // exact names, not prefixes
    assert(dc("dungeon clear run event") == ActionVerdict::Inert);
    assert(dc("dungeon clear razorgore") == ActionVerdict::Inert);

    // ---- five-man table ----
    assert(FindTableRow(578) && FindTableRow(578)->bossEntry == 27656 && FindTableRow(578)->guidSlot == 3);
    assert(!FindTableRow(409) && !FindTableRow(469) && !FindTableRow(0));

    // ---- group state: a group that leaves the map starts clean ----
    GroupState s;
    Follow(s, 469, 7);
    assert(Evaluate(s, 1000, [] { return true; }));
    assert(Evaluate(s, 1020, [] { return false; }));  // memo: same tick, same answer
    Follow(s, 469, 7);
    assert(s.active);
    Follow(s, 0, 0);  // left for Eastern Kingdoms
    assert(!s.active && s.signalMs == 0 && s.evalMs == 0);
    Follow(s, 469, 8);  // a new instance of the same raid
    assert(!s.active && s.mapId == 469 && s.instanceId == 8);

    // ---- NON-REGRESSION against Dungeon Clear 2.9.4 ----
    // (1) Update, every combination around the grace boundary and the wraparound.
    std::uint32_t const times[] = {0u, 1u, 49u, 50u, 2999u, 3000u, 3001u, 10000u, 0x7FFFFFFFu, 0xFFFFFF00u, 0xFFFFFFFFu};
    unsigned checked = 0;
    for (bool was : {false, true})
        for (std::uint32_t last : times)
            for (bool sig : {false, true})
                for (std::uint32_t now : times)
                {
                    Verdict const m = Update(was, last, sig, now);
                    DcVerdict const d = DcUpdate(was, last, sig, now);
                    assert(m.active == d.active && m.lastSignalMs == d.lastSignalMs);
                    ++checked;
                }

    // (2) ClassifyAction over the exempt names, near-misses and stock names, both prefixes.
    for (char const* n : {"dungeon clear run event combat", "dungeon clear razorgore orb", "dungeon clear razorgore camp",
                          "dungeon clear hold fire", "dungeon clear oc rider", "dungeon clear kz chess",
                          "dungeon clear assist camp combat", "dungeon clear assist camp", "dungeon clear run event",
                          "dungeon clear hazard vacate", "dungeon clear engage trash", "drop target", "attack",
                          "dps assist", "reach spell", ""})
        for (bool isDc : {false, true})
        {
            assert(AsInt(ClassifyAction(n, isDc)) == DcClassify(n, isDc));
            ++checked;
        }

    // (3) The per-group evaluation against Dungeon Clear's per-leader one: the same signal sequence, sampled at the
    // same instants (several members per tick, ticks 100-400ms apart, encounters blinking), gives the same verdicts.
    std::uint32_t seed = 2941u;
    for (int run = 0; run < 200; ++run)
    {
        GroupState mod;
        Follow(mod, 409, 1);
        DcRunState ref;
        std::uint32_t now = (run % 2) ? 0xFFFF0000u : 1000u;  // half the runs cross the wraparound
        bool encounter = false;
        for (int tick = 0; tick < 400; ++tick)
        {
            now += 100 + Next(seed) % 300;
            if (Next(seed) % 10 == 0)
                encounter = !encounter;
            bool const blink = encounter && Next(seed) % 8 == 0;  // a submerge or RP pause
            bool const signal = encounter && !blink;
            unsigned const members = 1 + Next(seed) % 5;
            for (unsigned m = 0; m < members; ++m)
            {
                std::uint32_t const at = now + m * 7;  // members of one tick, a few ms apart
                bool const got = Evaluate(mod, at, [signal] { return signal; });
                bool const want = DcIsActiveOnLeader(ref, at, signal);
                assert(got == want);
                ++checked;
            }
        }
    }

    std::printf("boss stand down: %u verdicts identical to Dungeon Clear 2.9.4\n", checked);
    return 0;
}
