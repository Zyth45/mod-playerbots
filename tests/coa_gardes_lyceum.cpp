// Blackrock Depths' Lyceum close-up (src/Ai/Coa/Gardes/Brd): Dungeon Clear's t/TestDungeonEvent.cpp Lyceum cases as
// asserts, then the NON-REGRESSION half against Dungeon Clear 2.9.4 (tests/dc294) with a bot tank driving a run.
#include "Ai/Coa/Gardes/Brd/CoaLyceumCloseUpDecision.h"

#include "dc294/Dc294Glue.h"

#include <cassert>
#include <cmath>

namespace
{
    bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

    void KernelCases()
    {
        using namespace CoaLyceumCloseUp;
        assert(!Decide(1340.0f, -450.0f, 1340.0f, -440.0f, false).close);  // 10yd
        assert(!Decide(1340.0f, -455.0f, 1340.0f, -440.0f, false).close);  // 15yd, on the line

        Verdict const v = Decide(1340.0f, -465.0f, 1340.0f, -440.0f, false);  // 25yd south
        assert(v.close);
        assert(Near(Dist2d(v.x, v.y, 1340.0f, -440.0f), AIM, 0.01f));
        assert(v.y < -440.0f);  // stays south of the tank, never walks through the adds
        assert(Near(v.x, 1340.0f, 0.01f));

        assert(Decide(1340.0f, -452.0f, 1340.0f, -440.0f, true).close);   // closing keeps walking at 12yd...
        assert(!Decide(1340.0f, -448.0f, 1340.0f, -440.0f, true).close);  // ...until inside CLOSE_TO
        static_assert(AIM < CLOSE_TO && CLOSE_TO < LEASH, "hysteresis");

        assert(!ChaseLeavesThePack(1345.0f, -440.0f, 1340.0f, -440.0f));
        assert(ChaseLeavesThePack(1365.0f, -440.0f, 1340.0f, -440.0f));

        // The room: the Lyceum entry and both braziers in, Magmus (the Iron Hall) and the tomb hold out.
        assert(InRoom(1311.5f, -353.0f, -92.0f));
        assert(InRoom(1330.1f, -503.0f, -88.9f));
        assert(InRoom(1431.1f, -503.0f, -88.9f));
        assert(!InRoom(1380.72f, -659.31f, -91.97f));
        assert(!InRoom(1268.0f, -272.0f, -78.1f));
        static_assert(LEASH < 22.0f, "the fight rule is tighter than the walking pack leash");
    }

    struct Lcg
    {
        uint32_t s = 4242u;
        uint32_t Next() { return s = s * 1664525u + 1013904223u; }
        bool Bit() { return (Next() >> 16) & 1u; }
        float Range(float lo, float hi) { return lo + (hi - lo) * float((Next() >> 8) & 0xFFFF) / 65535.0f; }
    };

    void KernelMatchesDc294()
    {
        Lcg r;
        for (int i = 0; i < 200000; ++i)
        {
            float const bx = r.Range(1250, 1500), by = r.Range(-580, -290), bz = r.Range(-110, -70);
            float const tx = r.Range(1250, 1500), ty = r.Range(-580, -290);
            float const ux = tx + r.Range(-30, 30), uy = ty + r.Range(-30, 30);
            bool const closing = r.Bit();
            assert(CoaLyceumCloseUp::InRoom(bx, by, bz) == DcLyceumCloseUp::InRoom(bx, by, bz));
            CoaLyceumCloseUp::Verdict const a = CoaLyceumCloseUp::Decide(ux, uy, tx, ty, closing);
            DcLyceumCloseUp::Verdict const b = DcLyceumCloseUp::Decide(ux, uy, tx, ty, closing);
            assert(a.close == b.close && a.x == b.x && a.y == b.y);
            assert(CoaLyceumCloseUp::ChaseLeavesThePack(bx, by, tx, ty) ==
                   DcLyceumCloseUp::ChaseLeavesThePack(bx, by, tx, ty));
        }
    }

    // Both halves' gates, bot tank driving: the run leader IS the reference tank, so "the bot is the Dungeon Clear
    // leader" and "the bot is the reference tank" are one fact.
    void GatesMatchDc294WithABotTankDriving()
    {
        for (int mask = 0; mask < (1 << 9); ++mask)
        {
            Dc294Glue::LyceumFacts f;
            f.reachAction = mask & 1;
            f.botInRoom = mask & 2;
            f.partyTank = mask & 4;
            f.tankIsBot = mask & 8;
            f.tankAlive = mask & 16;
            f.sameInstance = mask & 32;
            f.tankInRoom = mask & 64;
            f.hasTarget = mask & 128;
            bool const inCombat = mask & 256;
            f.isDcLeader = f.tankIsBot;
            f.tankX = 1340.0f;
            f.tankY = -440.0f;
            for (float tx : {1345.0f, 1365.0f})
                for (int botIsTank = 0; botIsTank < 2; ++botIsTank)
                {
                    f.targetX = tx;
                    f.targetY = -440.0f;
                    CoaLyceumCloseUp::Gate g;
                    g.hasTank = f.partyTank;
                    g.tankIsSelf = f.tankIsBot;
                    g.tankAlive = f.tankAlive;
                    g.sameInstance = f.sameInstance;
                    g.tankIsHuman = false;
                    g.botIsTank = botIsTank;
                    g.botInRoom = f.botInRoom;
                    g.tankInRoom = f.tankInRoom;
                    // The chase half, as CoaGardesBans.cpp composes it.
                    bool const coaChase = f.reachAction && f.botInRoom && CoaLyceumCloseUp::Applies(g) && f.hasTarget &&
                                          CoaLyceumCloseUp::ChaseLeavesThePack(f.targetX, f.targetY, f.tankX, f.tankY);
                    assert(coaChase == Dc294Glue::LyceumChaseLeavesThePack(f));
                    // The walk half's gate, as CoaBrdGuards.cpp's ProbeLyceum composes it.
                    assert((inCombat && CoaLyceumCloseUp::Applies(g)) == Dc294Glue::LyceumProbeApplies(f, inCombat));
                }
        }
    }

    void HumanMainTank()
    {
        CoaLyceumCloseUp::Gate g;
        g.hasTank = g.botInRoom = g.tankInRoom = true;
        g.tankIsHuman = true;
        g.botIsTank = true;
        assert(!CoaLyceumCloseUp::Applies(g));  // his off-tank is never drawn in
        g.botIsTank = false;
        assert(CoaLyceumCloseUp::Applies(g));
        g.tankAlive = false;
        assert(!CoaLyceumCloseUp::Applies(g));
    }
}

int main()
{
    KernelCases();
    KernelMatchesDc294();
    GatesMatchDc294WithABotTankDriving();
    HumanMainTank();
}
