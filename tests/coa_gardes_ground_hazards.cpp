// Ground hazards guard (moved from Dungeon Clear): registry, geometry and the vacate verdicts.
#include "Ai/Coa/Gardes/GroundHazards/CoaHazardKernel.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

    // ---- Dungeon Clear 2.9.4, verbatim (DcHazardRegistry.cpp rows and DcHazard.cpp / DcHazardRegistry.cpp kernels).
    // The non-regression reference: the module must give the same verdict for the same inputs.
    struct DcRow
    {
        int kind;  // 0 creature, 1 ground pool, 2 trap
        std::uint32_t mapId, key;
        float radius, zBand, vacateRadius, holdBand, retreatSlack;
    };

    DcRow const kDc294[] = {
        {0, 578, 28166, 6.0f, 8.0f, 8.0f, 2.0f, 6.0f},     {0, 668, 36954, 12.0f, 10.0f, 0.0f, 2.0f, 6.0f},
        {0, 650, 35614, 11.0f, 12.0f, 8.0f, 2.0f, 6.0f},   {0, 552, 20869, 22.0f, 12.0f, 0.0f, 2.0f, 6.0f},
        {0, 552, 21761, 15.0f, 12.0f, 15.0f, 2.0f, 6.0f},  {0, 552, 21303, 12.0f, 8.0f, 0.0f, 2.0f, 6.0f},
        {0, 552, 21304, 12.0f, 8.0f, 0.0f, 2.0f, 6.0f},    {0, 349, 12222, 8.0f, 6.0f, 5.0f, 6.0f, 9.0f},
        {0, 574, 23997, 7.0f, 10.0f, 5.0f, 2.0f, 6.0f},    {0, 599, 28265, 8.0f, 8.0f, 5.0f, 2.0f, 6.0f},
        {0, 599, 28237, 8.0f, 12.0f, 5.0f, 2.0f, 6.0f},    {0, 230, 108982, 30.0f, 12.0f, 0.0f, 2.0f, 6.0f},
        {0, 230, 8982, 30.0f, 12.0f, 0.0f, 2.0f, 6.0f},    {0, 409, 80644, 8.0f, 8.0f, 6.0f, 2.0f, 6.0f},
        {1, 289, 17742, 8.0f, 6.0f, 5.0f, 2.0f, 6.0f},     {1, 349, 21070, 8.0f, 6.0f, 5.0f, 2.0f, 6.0f},
        {1, 601, 53400, 8.0f, 6.0f, 5.0f, 2.0f, 6.0f},     {1, 601, 59419, 8.0f, 6.0f, 5.0f, 2.0f, 6.0f},
        {1, 600, 47346, 14.0f, 6.0f, 11.0f, 2.0f, 6.0f},   {1, 600, 49034, 10.0f, 6.0f, 8.0f, 2.0f, 6.0f},
        {1, 600, 49548, 10.0f, 6.0f, 8.0f, 2.0f, 6.0f},    {1, 608, 58693, 12.0f, 6.0f, 10.0f, 2.0f, 6.0f},
        {1, 604, 55627, 6.0f, 6.0f, 3.0f, 2.0f, 6.0f},     {1, 658, 69024, 7.0f, 6.0f, 4.0f, 2.0f, 6.0f},
        {1, 658, 70274, 7.0f, 6.0f, 4.0f, 2.0f, 6.0f},     {1, 668, 72362, 6.0f, 6.0f, 3.0f, 2.0f, 6.0f},
        {1, 409, 19717, 13.0f, 6.0f, 10.0f, 2.0f, 6.0f},   {2, 540, 181915, 5.0f, 6.0f, 3.5f, 2.0f, 6.0f},
        {2, 229, 175124, 3.0f, 4.0f, 0.0f, 2.0f, 6.0f},    {2, 409, 178164, 6.0f, 4.0f, 5.0f, 2.0f, 6.0f},
        {2, 409, 177704, 7.0f, 6.0f, 5.0f, 2.0f, 6.0f},
    };

    float DcDistSqToSegment2D(float px, float py, float ax, float ay, float bx, float by)
    {
        float const ex = bx - ax, ey = by - ay;
        float const len2 = ex * ex + ey * ey;
        if (len2 <= 1e-6f)
            return (px - ax) * (px - ax) + (py - ay) * (py - ay);
        float t = ((px - ax) * ex + (py - ay) * ey) / len2;
        if (t < 0.0f)
            t = 0.0f;
        else if (t > 1.0f)
            t = 1.0f;
        float const dx = px - (ax + t * ex), dy = py - (ay + t * ey);
        return dx * dx + dy * dy;
    }

    bool DcPointInCylinder(float r, float zb, float ex, float ey, float ez, float px, float py, float pz)
    {
        if (r <= 0.0f || std::fabs(pz - ez) > zb)
            return false;
        float const dx = px - ex, dy = py - ey;
        return dx * dx + dy * dy < r * r;
    }

    bool DcSegmentClips(float r, float zb, float ex, float ey, float ez, float ax, float ay, float az, float bx,
                        float by, float bz)
    {
        if (r <= 0.0f)
            return false;
        float const da = az - ez, db = bz - ez;
        if (std::fabs(da) > zb && std::fabs(db) > zb && (da > 0.0f) == (db > 0.0f))
            return false;
        return DcDistSqToSegment2D(ex, ey, ax, ay, bx, by) < r * r;
    }

    struct DcLive
    {
        float x, y, z, radius, zBand, vacateRadius, holdBand, retreatSlack;
        bool alive;
    };

    bool DcInDanger(DcLive const& h, float px, float py, float pz)
    {
        if (h.vacateRadius <= 0.0f || !h.alive)
            return false;
        if (std::fabs(h.z - pz) > h.zBand)
            return false;
        float const reach = h.vacateRadius + h.holdBand;
        float const dx = px - h.x, dy = py - h.y;
        return dx * dx + dy * dy <= reach * reach;
    }

    struct DcVacate
    {
        bool ok{false};
        float x{0}, y{0}, z{0}, pulse{0}, slack{6.0f};
    };

    DcVacate DcNearestVacate(std::vector<DcLive> const& live, float px, float py, float pz)
    {
        DcVacate best;
        float bestDistSq = std::numeric_limits<float>::max();
        for (DcLive const& h : live)
        {
            if (!DcInDanger(h, px, py, pz))
                continue;
            float const dx = px - h.x, dy = py - h.y;
            float const d = dx * dx + dy * dy;
            if (d >= bestDistSq)
                continue;
            bestDistSq = d;
            best = {true, h.x, h.y, h.z, h.vacateRadius, h.retreatSlack};
        }
        return best;
    }

    template <class Row>
    void SameRow(Row const* r, DcRow const& dc)
    {
        assert(r);
        assert(r->mapId == dc.mapId);
        assert(Near(r->radius, dc.radius) && Near(r->zBand, dc.zBand) && Near(r->vacateRadius, dc.vacateRadius));
        assert(Near(r->holdBand, dc.holdBand) && Near(r->retreatSlack, dc.retreatSlack));
    }
}

int main()
{
    namespace R = CoaHazardRegistry;
    using namespace CoaHazard;

    // ---- registry: which maps carry which kind (DC TestDcHazard ReportsMapsWithEmitters) ----
    assert(R::HasEmitters(552) && !R::HasEmitters(0) && !R::HasEmitters(554));
    assert(!R::HasEmitters(289) && R::HasGroundHazards(289) && !R::HasGroundHazards(552));
    assert(R::HasAnyHazard(289) && R::HasAnyHazard(552) && !R::HasAnyHazard(554) && !R::HasAnyHazard(0));
    assert(R::HasEmitters(349) && R::HasGroundHazards(349) && R::HasAnyHazard(349));
    assert(!R::HasEmitters(540) && !R::HasGroundHazards(540) && R::HasTrapHazards(540) && R::HasAnyHazard(540));
    assert(!R::HasEmitters(658) && R::HasGroundHazards(658));
    // Classic CoA maps: Molten Core, BRD, Scholomance, Maraudon, UBRS.
    assert(R::HasEmitters(409) && R::HasGroundHazards(409) && R::HasTrapHazards(409));
    assert(R::HasEmitters(230) && R::HasTrapHazards(229) && R::HasAnyHazard(289) && R::HasAnyHazard(349));

    // ---- keyed on both map and entry ----
    CoaHazardEmitter const* sentinel = R::Find(552, 20869);
    assert(sentinel && Near(sentinel->radius, 22.0f) && Near(sentinel->vacateRadius, 0.0f));
    CoaHazardEmitter const* destroyed = R::Find(552, 21761);
    assert(destroyed && Near(destroyed->vacateRadius, 15.0f) && Near(destroyed->radius, 15.0f));
    assert(!R::Find(552, 99999) && !R::Find(0, 20869));
    CoaGroundHazard const* cloud = R::FindGround(289, 17742);
    assert(cloud && Near(cloud->vacateRadius, 5.0f) && Near(cloud->radius, 8.0f));
    assert(!R::FindGround(289, 29047) && !R::FindGround(552, 17742));
    CoaTrapHazard const* blaze = R::FindTrap(540, 181915);
    assert(blaze && Near(blaze->vacateRadius, 3.5f) && Near(blaze->radius, 5.0f));
    assert(!R::FindTrap(540, 181914) && !R::FindTrap(289, 181915));
    assert(R::TrapEntries(409).size() == 2 && R::TrapEntries(0).empty());
    assert(!R::Find(349, 12221));  // the Noxious Slime emits nothing itself

    // ---- the Creeping Sludge stays out: hold band wider than melee reach, retreat past it ----
    CoaHazardEmitter const* sludge = R::Find(349, 12222);
    assert(sludge && sludge->holdBand > 4.75f && Near(sludge->retreatSlack, 9.0f));
    assert(sludge->vacateRadius + sludge->retreatSlack - (sludge->vacateRadius + sludge->holdBand) >= 2.0f);

    // ---- every vacate row overshoots its hold band, and its aim point clears its own keep-out ----
    for (auto const& e : CoaHazardData::kEmitters)
        if (e.vacateRadius > 0.0f)
            assert(e.retreatSlack > e.holdBand);
    for (auto const& g : CoaHazardData::kGroundHazards)
    {
        assert(g.vacateRadius > 0.0f && g.retreatSlack > g.holdBand);
        float const aim = g.vacateRadius + g.retreatSlack;
        assert(!R::PointInside(g, 0, 0, 0, aim, 0, 0) && R::PointInside(g, 0, 0, 0, 0, 0, 0));
    }
    for (auto const& t : CoaHazardData::kTrapHazards)
        if (t.vacateRadius > 0.0f)
        {
            float const aim = t.vacateRadius + t.retreatSlack;
            assert(t.retreatSlack > t.holdBand && !R::PointInside(t, 0, 0, 0, aim, 0, 0));
        }

    // ---- geometry (PointInsideRespectsRadius / ZBand, SegmentClips*, ZeroRadius) ----
    CoaHazardEmitter e{552, 20869, 22.0f, 12.0f};
    assert(R::PointInside(e, 0, 0, 0, 21.5f, 0, 0) && !R::PointInside(e, 0, 0, 0, 22.5f, 0, 0));
    assert(!R::PointInside(e, 0, 0, 22.4f, 0, 0, 48.4f) && R::PointInside(e, 0, 0, 22.4f, 0, 0, 30.0f));
    assert(R::SegmentClips(e, 0, 0, 0, -50, 0, 0, 50, 0, 0) && !R::SegmentClips(e, 0, 0, 0, -50, 40, 0, 50, 40, 0));
    assert(!R::SegmentClips(e, 0, 0, 22.4f, -50, 0, 48.4f, 50, 0, 48.4f));
    assert(R::SegmentClips(e, 0, 0, 22.4f, -10, 0, 24.0f, 50, 0, 48.4f));
    assert(R::SegmentClips(e, 0, 0, 22.4f, 0, 0, 48.0f, 0, 0, -10.0f));  // straddles the band
    assert(!R::SegmentClips(e, 0, 0, 22.4f, 40, 0, 48.0f, 40, 0, -10.0f));
    CoaHazardEmitter zero{552, 20869, 0.0f, 12.0f};
    assert(!R::PointInside(zero, 0, 0, 0, 0, 0, 0) && !R::SegmentClips(zero, 0, 0, 0, -50, 0, 0, 50, 0, 0));

    // ---- detour bound (DcHazardVacateDetourTest) ----
    assert(!(60.0f <= DetourBound(14.0f, VacateDetourRatio, VacateDetourSlack)));  // the long way round a wall
    assert(10.0f <= DetourBound(3.0f, VacateDetourRatio, VacateDetourSlack));      // slack carries short range
    assert(VacateCommitMaxMs < 47000);

    // ---- the vacate verdicts ----
    LiveSet live;
    live.push_back(FromRow(*sludge, 0, 0, 0, true));
    assert(NearestVacate(live, 10.9f, 0, 0).ok);    // inside 5 + 6 hold band
    assert(!NearestVacate(live, 11.1f, 0, 0).ok);
    live[0].alive = false;  // a dead emitter has stopped pulsing
    assert(!NearestVacate(live, 1.0f, 0, 0).ok && PointIsHot(live, 1.0f, 0, 0));
    live[0].alive = true;
    assert(!NearestVacate(live, 0, 0, 30.0f).ok);  // other floor

    // ---- NON-REGRESSION against Dungeon Clear 2.9.4: same rows, same verdicts for the same inputs ----
    unsigned dcRows[3] = {0, 0, 0};
    for (DcRow const& dc : kDc294)
    {
        ++dcRows[dc.kind];
        if (dc.kind == 0)
            SameRow(R::Find(dc.mapId, dc.key), dc);
        else if (dc.kind == 1)
            SameRow(R::FindGround(dc.mapId, dc.key), dc);
        else
            SameRow(R::FindTrap(dc.mapId, dc.key), dc);
    }
    assert(dcRows[0] == CoaHazardData::kEmitters.size() && dcRows[1] == CoaHazardData::kGroundHazards.size() &&
           dcRows[2] == CoaHazardData::kTrapHazards.size());

    // A crowded live world (overlapping sludges, a pool, a trap, a dead emitter, another floor) swept on a grid.
    std::vector<DcLive> dcLive;
    LiveSet modLive;
    struct Placed
    {
        int row;
        float x, y, z;
        bool alive;
    };
    Placed const placed[] = {{7, 0, 0, 0, true},    {7, 6, 3, 0.5f, true}, {7, -4, 9, 0, false}, {15, 3, -5, 0, true},
                             {28, -8, -2, 1, true}, {4, 20, 20, 0, true},  {3, -20, 5, 0, true}, {13, 0, 0, 40, true}};
    for (Placed const& p : placed)
    {
        DcRow const& r = kDc294[p.row];
        dcLive.push_back({p.x, p.y, p.z, r.radius, r.zBand, r.vacateRadius, r.holdBand, r.retreatSlack, p.alive});
        modLive.push_back(LiveHazard{p.x, p.y, p.z, r.radius, r.zBand, r.vacateRadius, r.holdBand, r.retreatSlack,
                                     p.alive});
    }

    unsigned checked = 0, inDanger = 0;
    for (float x = -45.0f; x <= 45.0f; x += 0.75f)
        for (float y = -45.0f; y <= 45.0f; y += 0.75f)
            for (float z : {0.0f, 3.0f, 9.0f, 40.0f})
            {
                DcVacate const want = DcNearestVacate(dcLive, x, y, z);
                VacateEmitter const got = NearestVacate(modLive, x, y, z);
                assert(want.ok == got.ok);
                if (want.ok)
                {
                    ++inDanger;
                    assert(want.x == got.x && want.y == got.y && want.z == got.z);
                    assert(want.pulse == got.pulseRadius && want.slack == got.retreatSlack);
                }

                bool dcHot = false, dcBand = false, dcSeg = false;
                for (DcLive const& h : dcLive)
                {
                    dcHot |= DcPointInCylinder(h.radius, h.zBand, h.x, h.y, h.z, x, y, z);
                    dcBand |= DcInDanger(h, x, y, z);
                    dcSeg |= DcSegmentClips(h.radius, h.zBand, h.x, h.y, h.z, x, y, z, -x * 0.5f, y + 7.0f, 0.0f);
                }
                assert(dcHot == PointIsHot(modLive, x, y, z));
                assert(dcBand == PointIsInVacateBand(modLive, x, y, z));
                assert(dcSeg == SegmentIsHot(modLive, x, y, z, -x * 0.5f, y + 7.0f, 0.0f));
                ++checked;
            }
    assert(inDanger > 100);

    std::printf("ground hazards: %u points, %u in danger, identical to Dungeon Clear 2.9.4\n", checked, inDanger);
    return 0;
}
