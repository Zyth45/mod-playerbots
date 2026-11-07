// Blackrock Depths' Iron Hall safe spot (src/Ai/Coa/Gardes/Brd): Dungeon Clear's t/TestDcHazard.cpp Iron Hall cases
// as asserts, then the NON-REGRESSION half against Dungeon Clear 2.9.4 (tests/dc294) with a bot tank driving a run.
#include "Ai/Coa/Gardes/Brd/CoaIronHallDecision.h"

#include "dc294/Dc294IronHallDecision.h"

#include <cassert>
#include <cmath>
#include <initializer_list>

namespace
{
    namespace B = CoaBrd;
    constexpr float MAGMUS_X = 1380.72f;
    constexpr float MAGMUS_Y = -659.31f;

    void Cases()
    {
        assert(B::IRON_HALL_FIRE_KEEPOUT >= 30.0f + 1.5f);  // the raw circle plus the unit's reach
        for (uint32_t z = 0; z < B::IRON_HALL_ZONE_COUNT; ++z)
            for (uint32_t s = 0; s < B::IRON_HALL_SLOT_COUNT; ++s)
            {
                CoaIronHall::Spot const p = CoaIronHall::SlotOf(z, s);
                assert(CoaIronHall::FireDistance(p.x, p.y) >= B::IRON_HALL_FIRE_KEEPOUT + 1.0f);
                assert(CoaIronHall::InHall(p.x, p.y, B::IRON_HALL_FLOOR_Z));
                assert(CoaIronHall::ZoneOf(p.x, p.y) == static_cast<int>(z));
            }
        for (uint32_t z = 1; z < B::IRON_HALL_ZONE_COUNT; ++z)
            assert(B::IRON_HALL_ZONES[z].y < B::IRON_HALL_ZONES[z - 1].y);
        assert(CoaIronHall::ZoneOf(MAGMUS_X, MAGMUS_Y) == 2);
        for (float y : {-587.3f, -632.6f, -677.5f})
        {
            assert(CoaIronHall::InFire(1380.5f, y));
            assert(CoaIronHall::ZoneOf(1380.5f, y) == -1);
        }
        assert(CoaIronHall::InHall(1389.55f, -555.85f, -92.0f));
        assert(CoaIronHall::InHall(1380.12f, -710.48f, -92.0f));
        assert(!CoaIronHall::InHall(1330.1f, -503.0f, -88.9f));     // the Lyceum
        assert(!CoaIronHall::InHall(1380.18f, -831.65f, -87.59f));  // the throne room

        // Hop only when stopped in the fire.
        CoaIronHall::Inputs in;
        in.goalX = MAGMUS_X;
        in.goalY = MAGMUS_Y;
        in.x = 1380.5f;
        in.y = -609.6f;
        assert(!CoaIronHall::Decide(in).hop);
        in.y = -632.6f;
        in.moving = true;
        assert(!CoaIronHall::Decide(in).hop);  // a follow / chase mid-crossing
        in.hasDest = true;
        in.destX = 1380.5f;
        in.destY = -655.7f;
        assert(!CoaIronHall::Decide(in).hop);  // heading for gap 2
        in.destY = -677.5f;
        CoaIronHall::Verdict v = CoaIronHall::Decide(in);
        assert(v.hop && v.redirect);
        in.isLeader = true;
        assert(!CoaIronHall::Decide(in).hop);  // never the leader
        in.moving = false;
        in.hasDest = false;
        v = CoaIronHall::Decide(in);
        assert(v.hop && !v.redirect && v.zone == 2u && !CoaIronHall::InFire(v.spot.x, v.spot.y));
        in.isLeader = false;
        in.goalX = 1380.5f;
        in.goalY = -562.5f;
        assert(CoaIronHall::Decide(in).zone == 1u);

        // In a fight, the stop keeps the target in reach.
        CoaIronHall::Inputs f;
        f.hasTarget = true;
        f.targetX = MAGMUS_X;
        f.targetY = MAGMUS_Y;
        f.goalX = 1380.5f;
        f.goalY = -562.5f;
        f.x = MAGMUS_X + 1.0f;
        f.y = MAGMUS_Y - 5.0f;
        assert(CoaIronHall::InFire(f.x, f.y));
        f.reach = 4.5f;
        v = CoaIronHall::Decide(f);
        assert(v.hop && v.inReach && v.zone == 2u);
        assert(std::hypot(v.spot.x - MAGMUS_X, v.spot.y - MAGMUS_Y) <= f.reach);
        f.x = 1380.5f;
        f.y = -637.0f;
        assert(CoaIronHall::InFire(f.x, f.y));
        f.reach = 26.5f;
        v = CoaIronHall::Decide(f);
        assert(v.hop && v.inReach && v.zone == 2u);
        f.targetX = 1380.18f;
        f.targetY = -831.65f;
        f.reach = 4.5f;
        v = CoaIronHall::Decide(f);
        assert(v.hop && !v.inReach && v.zone == 1u);

        // The follow clamp.
        CoaIronHall::Spot const g2a = CoaIronHall::SlotOf(2, 0);
        CoaIronHall::Spot const g2b = CoaIronHall::SlotOf(2, 3);
        CoaIronHall::Spot const g1 = CoaIronHall::SlotOf(1, 0);
        assert(CoaIronHall::HoldFollow(g2a.x, g2a.y, g2b.x, g2b.y));
        assert(!CoaIronHall::HoldFollow(g1.x, g1.y, g2b.x, g2b.y));
        assert(!CoaIronHall::HoldFollow(g2a.x, g2a.y, 1380.5f, -677.5f));
        assert(!CoaIronHall::HoldFollow(1380.5f, -632.6f, g2b.x, g2b.y));
    }

    struct Lcg
    {
        uint32_t s = 99u;
        uint32_t Next() { return s = s * 1664525u + 1013904223u; }
        bool Bit() { return (Next() >> 16) & 1u; }
        float Range(float lo, float hi) { return lo + (hi - lo) * float((Next() >> 8) & 0xFFFF) / 65535.0f; }
    };

    // NON-REGRESSION: the kernel, every input crossed. The glue gate is a reference tank present (2.9.4: PartyTank),
    // and "the bot is the leader" is "the bot is the reference tank" in a run: both read the same fact.
    void MatchesDc294()
    {
        Lcg r;
        for (int i = 0; i < 300000; ++i)
        {
            CoaIronHall::Inputs a;
            a.x = r.Range(1335, 1425);
            a.y = r.Range(-715, -550);
            a.moving = r.Bit();
            a.hasDest = r.Bit();
            a.destX = r.Range(1335, 1425);
            a.destY = r.Range(-715, -550);
            a.isLeader = (r.Next() % 5) == 0;
            a.goalX = r.Range(1335, 1425);
            a.goalY = r.Range(-715, -550);
            a.hasTarget = r.Bit();
            a.targetX = r.Range(1335, 1425);
            a.targetY = r.Range(-715, -550);
            a.reach = r.Range(3, 35);
            a.slotSeed = r.Next();
            DcIronHall::Inputs b;
            b.x = a.x;
            b.y = a.y;
            b.moving = a.moving;
            b.hasDest = a.hasDest;
            b.destX = a.destX;
            b.destY = a.destY;
            b.isLeader = a.isLeader;
            b.goalX = a.goalX;
            b.goalY = a.goalY;
            b.hasTarget = a.hasTarget;
            b.targetX = a.targetX;
            b.targetY = a.targetY;
            b.reach = a.reach;
            b.slotSeed = a.slotSeed;
            CoaIronHall::Verdict const va = CoaIronHall::Decide(a);
            DcIronHall::Verdict const vb = DcIronHall::Decide(b);
            assert(va.hop == vb.hop && va.spot.x == vb.spot.x && va.spot.y == vb.spot.y && va.zone == vb.zone &&
                   va.inReach == vb.inReach && va.redirect == vb.redirect);
            float const z = r.Range(-105, -80);
            assert(CoaIronHall::InHall(a.x, a.y, z) == DcIronHall::InHall(a.x, a.y, z));
            assert(CoaIronHall::ZoneOf(a.x, a.y) == DcIronHall::ZoneOf(a.x, a.y));
            assert(CoaIronHall::HoldFollow(a.x, a.y, a.goalX, a.goalY) == DcIronHall::HoldFollow(b.x, b.y, b.goalX, b.goalY));
        }
    }
}

int main()
{
    Cases();
    MatchesDc294();
}
