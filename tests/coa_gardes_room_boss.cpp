// The sleeping room boss chase guard (src/Ai/Coa/Gardes/RoomBoss): Dungeon Clear's chase cases
// (t/TestDungeonClearMath.cpp) and registry cases (t/TestRoomAggro.cpp, CoA rows) as asserts, then the NON-REGRESSION
// half against Dungeon Clear 2.9.4 (tests/dc294) with a bot tank driving an active run.
#include "Ai/Coa/Gardes/RoomBoss/CoaRoomBossChase.h"

#include "dc294/Dc294Glue.h"

#include <cassert>
#include <cmath>
#include <set>

namespace
{
    bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

    void ChaseCases()
    {
        using CoaRoomBossChase::ChaseEndToBoss;
        using CoaRoomBossChase::ChaseWakesSleepingBoss;
        // The planted Doomforge Dragoon 26.4yd from the Emperor (sphere 28.3): a caster stopping at spell range ends
        // outside the sphere, melee and a leap end on the target, inside.
        float const caster = ChaseEndToBoss(74.4f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 30.0f);
        assert(Near(caster, 56.4f, 0.01f));
        assert(!ChaseWakesSleepingBoss(caster, 28.3f, false));
        float const melee = ChaseEndToBoss(74.4f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 0.0f);
        assert(Near(melee, 26.4f, 0.01f));
        assert(ChaseWakesSleepingBoss(melee, 28.3f, false));
        assert(Near(ChaseEndToBoss(40.0f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 30.0f), 40.0f, 0.01f));
        float const fromBehind = ChaseEndToBoss(-10.0f, 0.0f, 26.4f, 0.0f, 0.0f, 0.0f, 30.0f);
        assert(Near(fromBehind, 3.6f, 0.01f));
        assert(ChaseWakesSleepingBoss(fromBehind, 28.3f, false));
        // The fight carried the tank 22yd from a sleeping Emperor; once he is up the chase is free.
        assert(ChaseWakesSleepingBoss(21.0f, 28.3f, false));
        assert(!ChaseWakesSleepingBoss(35.0f, 28.3f, false));
        assert(!ChaseWakesSleepingBoss(21.0f, 28.3f, true));
    }

    void RegistryCases()
    {
        assert(CoaRoomBossChase::SkirtOverride(36, 1763) == 0.0f);   // Gilnid: no override
        assert(CoaRoomBossChase::SkirtOverride(36, 12345) == 0.0f);  // not a room boss
        assert(CoaRoomBossChase::Find(230, 9019) != nullptr);        // the Emperor
        assert(CoaRoomBossChase::ForMap(189).size() == 2u);          // Mograine and Whitemane
        assert(CoaRoomBossChase::ForMap(0).empty());
        // CoA content only: no BC or WotLK row.
        for (unsigned map : {555u, 557u, 601u, 650u, 604u, 554u, 532u})
            assert(CoaRoomBossChase::ForMap(map).empty());
        // The sphere is widened by the skirt, never shrunk.
        assert(CoaRoomBossChase::WakeSphere(28.0f, 0.0f) == 28.0f);
        assert(CoaRoomBossChase::WakeSphere(28.0f, 40.0f) == 40.0f);
        assert(CoaRoomBossChase::WakeSphere(28.0f, 20.0f) == 28.0f);
    }

    // NON-REGRESSION: the module's rows are exactly Dungeon Clear 2.9.4's rows on CoA's classic maps.
    void RowsMatchDc294()
    {
        std::set<unsigned> const classic = {36u, 109u, 189u, 229u, 230u, 289u, 429u};
        unsigned dcClassic = 0;
        for (RoomAggroBoss const& row : Dc294RoomAggro::kRoomAggroBosses)
        {
            if (!classic.count(row.mapId))
                continue;
            ++dcClassic;
            CoaRoomBossChase::RoomBoss const* mine = CoaRoomBossChase::Find(row.mapId, row.bossEntry);
            assert(mine && mine->skirtRadius == row.skirtRadius);
        }
        unsigned mineCount = 0;
        for (CoaRoomBossChase::RoomBoss const& b : CoaRoomBossChase::ROOM_BOSSES)
        {
            ++mineCount;
            assert(Dc294RoomAggro::Find(b.mapId, b.bossEntry) != nullptr);
        }
        assert(mineCount == dcClassic);
    }

    struct Lcg
    {
        uint32_t s = 777u;
        uint32_t Next() { return s = s * 1664525u + 1013904223u; }
        bool Bit() { return (Next() >> 16) & 1u; }
        float Range(float lo, float hi) { return lo + (hi - lo) * float((Next() >> 8) & 0xFFFF) / 65535.0f; }
    };

    // NON-REGRESSION: geometry and sphere sizing on a dense sample.
    void GeometryMatchesDc294()
    {
        Lcg r;
        for (int i = 0; i < 200000; ++i)
        {
            float const v[7] = {r.Range(-80, 80), r.Range(-80, 80), r.Range(-80, 80), r.Range(-80, 80),
                                r.Range(-80, 80), r.Range(-80, 80), (r.Next() % 3) ? 0.0f : r.Range(0, 40)};
            float const a = CoaRoomBossChase::ChaseEndToBoss(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
            float const b = DungeonClearMath::ChaseEndToBoss(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
            assert(a == b);
            float const sphere = r.Range(0, 60);
            bool const inCombat = r.Bit();
            assert(CoaRoomBossChase::ChaseWakesSleepingBoss(a, sphere, inCombat) ==
                   DungeonClearMath::ChaseWakesSleepingBoss(b, sphere, inCombat));
            float const aggro = r.Range(5, 45), mobReach = r.Range(0, 8), botReach = r.Range(0, 2);
            float const margin = r.Range(0, 10), padding = r.Range(0, 30);
            float const computed = CoaRoomBossChase::AggroReachYards(aggro, mobReach, botReach, margin, padding);
            assert(computed == DcEngageGeometry::AggroReachYards(aggro, mobReach, botReach, margin, padding));
            for (RoomAggroBoss const& row : Dc294RoomAggro::kRoomAggroBosses)
                if (CoaRoomBossChase::Find(row.mapId, row.bossEntry))
                {
                    float const skirt = Dc294RoomAggro::SkirtOverride(row.mapId, row.bossEntry);
                    float const dcSphere = skirt > computed ? skirt : computed;  // RoomAggroSphereRadius's tail
                    assert(CoaRoomBossChase::WakeSphere(
                               computed, CoaRoomBossChase::SkirtOverride(row.mapId, row.bossEntry)) == dcSphere);
                }
        }
    }

    // NON-REGRESSION: the whole ban, bot tank driving a run. The module side is composed exactly as
    // CoaGardesBans.cpp's ChaseWakesSleepingRoomBoss: the run hook gives the boss Dungeon Clear's room clear names.
    void BanMatchesDc294WithABotTankDriving()
    {
        Lcg r;
        for (int i = 0; i < 200000; ++i)
        {
            Dc294Glue::RoomBossFacts f;
            f.chaseAction = (r.Next() % 8) != 0;
            f.partyTank = (r.Next() % 10) != 0;
            f.tankAlive = (r.Next() % 10) != 0;
            f.sameInstance = (r.Next() % 10) != 0;
            f.tankHasAI = true;  // a bot tank drives
            f.roomClearActive = r.Bit();
            f.hasNextBoss = (r.Next() % 6) != 0;
            f.liveBoss = (r.Next() % 6) != 0;
            f.hasTarget = (r.Next() % 8) != 0;
            f.targetIsBoss = (r.Next() % 8) == 0;
            f.bossInCombat = (r.Next() % 4) == 0;
            f.bossStandDown = (r.Next() % 6) == 0;
            f.botX = r.Range(-60, 60);
            f.botY = r.Range(-60, 60);
            f.targetX = r.Range(-40, 40);
            f.targetY = r.Range(-40, 40);
            f.range = (r.Next() % 3) ? 0.0f : r.Range(20, 36);
            f.sphere = r.Range(15, 45);
            bool const dc = Dc294Glue::ChaseWakesSleepingRoomBoss(f);

            bool coa = false;
            CoaRoomBossChase::BranchInputs in;
            in.chaseAction = f.chaseAction;
            in.hasTank = f.partyTank && f.tankAlive && f.sameInstance;
            in.tankIsHuman = false;
            in.inDcRun = true;
            in.botIsTank = r.Bit();
            CoaRoomBossChase::Branch const branch = CoaRoomBossChase::ChooseBranch(in);
            assert(branch != CoaRoomBossChase::Branch::MapBosses);
            if (branch == CoaRoomBossChase::Branch::RunBoss && !f.bossStandDown && f.hasTarget)
            {
                bool const boss = f.roomClearActive && f.hasNextBoss && f.liveBoss;  // RoomClearBossOf(tank)
                if (boss && !f.targetIsBoss)
                    coa = CoaRoomBossChase::ChaseWakesSleepingBoss(
                        CoaRoomBossChase::ChaseEndToBoss(f.botX, f.botY, f.targetX, f.targetY, f.bossX, f.bossY,
                                                         f.range),
                        f.sphere, f.bossInCombat);
            }
            assert(coa == dc);
        }
    }

    // Outside a run: a human Main Tank's bot tanks are exempt; everybody else, and every member under a bot main tank,
    // is guarded against every sleeping flagged boss of the map.
    void BranchesOutsideARun()
    {
        CoaRoomBossChase::BranchInputs in;
        in.chaseAction = true;
        in.hasTank = true;
        in.tankIsHuman = true;
        in.botIsTank = true;
        assert(CoaRoomBossChase::ChooseBranch(in) == CoaRoomBossChase::Branch::None);
        in.botIsTank = false;
        assert(CoaRoomBossChase::ChooseBranch(in) == CoaRoomBossChase::Branch::MapBosses);
        in.tankIsHuman = false;
        in.botIsTank = true;
        assert(CoaRoomBossChase::ChooseBranch(in) == CoaRoomBossChase::Branch::MapBosses);
        in.hasTank = false;  // a dead reference tank is no reference
        assert(CoaRoomBossChase::ChooseBranch(in) == CoaRoomBossChase::Branch::None);
        in.hasTank = true;
        in.chaseAction = false;
        assert(CoaRoomBossChase::ChooseBranch(in) == CoaRoomBossChase::Branch::None);
    }
}

int main()
{
    ChaseCases();
    RegistryCases();
    RowsMatchDc294();
    GeometryMatchesDc294();
    BanMatchesDc294WithABotTankDriving();
    BranchesOutsideARun();
}
