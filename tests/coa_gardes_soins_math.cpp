#include "Ai/Coa/Gardes/Soins/CoaHealMath.h"

#include <cassert>
#include <cmath>

using namespace CoaHealMath;

namespace
{
bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

RepositionInputs Base()
{
    RepositionInputs in{};
    in.enabled = true;
    in.isHealer = true;
    in.inCombat = true;
    in.targetInCombat = true;
    in.hasTarget = true;
    in.targetDistance = 20.0f;
    in.maxRange = 60.0f;
    in.targetInSight = false;
    in.healRange = 40.0f;
    return in;
}
}

int main()
{
    // Heal target: nobody below the floor.
    assert(SelectHealTarget({ { 95.0f, false }, { 92.0f, true } }, 90.0f, 15.0f) == HealTargetNone);
    assert(SelectHealTarget({}, 90.0f, 15.0f) == HealTargetNone);
    // The most hurt.
    assert(SelectHealTarget({ { 80.0f, false }, { 50.0f, false }, { 70.0f, false } }, 90.0f, 15.0f) == 1);
    // A hurt tank wins over a slightly more hurt DPS.
    assert(SelectHealTarget({ { 50.0f, false }, { 60.0f, true } }, 90.0f, 15.0f) == 1);
    // Not over a much more hurt one.
    assert(SelectHealTarget({ { 30.0f, false }, { 60.0f, true } }, 90.0f, 15.0f) == 0);
    // A healthy tank never steals the pick (raw floor).
    assert(SelectHealTarget({ { 85.0f, false }, { 95.0f, true } }, 90.0f, 15.0f) == 0);
    // At the floor is not below it.
    assert(SelectHealTarget({ { 90.0f, false } }, 90.0f, 15.0f) == HealTargetNone);

    // Ring: first point on the healer's side, then alternately either side.
    std::vector<Point2> ring = StandoffRing({ 0.0f, 0.0f }, { 10.0f, 0.0f }, 5.0f, 7);
    assert(ring.size() == 8);
    assert(Near(ring[0].x, 5.0f) && Near(ring[0].y, 0.0f));
    assert(ring[1].y > 0.0f && ring[2].y < 0.0f && Near(ring[1].x, ring[2].x));
    for (Point2 const& p : ring)
        assert(Near(std::hypot(p.x, p.y), 5.0f));
    // Healer on the center: +X first.
    ring = StandoffRing({ 3.0f, 4.0f }, { 3.0f, 4.0f }, 2.0f, 0);
    assert(ring.size() == 1 && Near(ring[0].x, 5.0f) && Near(ring[0].y, 4.0f));

    // Standoff radius.
    assert(Near(StandoffRadius(40.0f), 24.0f));
    assert(Near(StandoffRadius(4.0f), 5.0f));

    // Path end.
    assert(PathEndsAt(1.0f, 1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 5.0f, 5.0f));
    assert(!PathEndsAt(6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f, 5.0f));
    assert(!PathEndsAt(0.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 5.0f, 10.0f));

    // Point short of the route's end, along the route.
    struct P3 { float x, y, z; };
    float x, y, z;
    std::vector<P3> route = { { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 10.0f, 10.0f, 0.0f } };
    PointShortOfPathEnd(route, 5.0f, x, y, z);
    assert(Near(x, 10.0f) && Near(y, 5.0f));
    PointShortOfPathEnd(route, 15.0f, x, y, z);
    assert(Near(x, 5.0f) && Near(y, 0.0f));
    PointShortOfPathEnd(route, 50.0f, x, y, z);
    assert(Near(x, 0.0f) && Near(y, 0.0f));
    PointShortOfPathEnd(std::vector<P3>{}, 5.0f, x, y, z);
    assert(Near(x, 0.0f) && Near(y, 0.0f) && Near(z, 0.0f));

    // Reposition: out of sight -> walk.
    RepositionInputs in = Base();
    assert(ShouldReposition(in));
    // In sight and in range -> stay.
    in.targetInSight = true;
    assert(!ShouldReposition(in));
    // In sight but out of range -> walk.
    in.targetDistance = 50.0f;
    assert(ShouldReposition(in));
    // Too far -> a wipe, not a reposition.
    in = Base();
    in.targetDistance = 70.0f;
    assert(!ShouldReposition(in));
    // Visible heal work first.
    in = Base();
    in.visibleHurtInSight = true;
    assert(!ShouldReposition(in));
    // Off, not a healer, the tank itself, crowd controlled, no target.
    in = Base(); in.enabled = false; assert(!ShouldReposition(in));
    in = Base(); in.isHealer = false; assert(!ShouldReposition(in));
    in = Base(); in.isReferenceTank = true; assert(!ShouldReposition(in));
    in = Base(); in.crowdControlled = true; assert(!ShouldReposition(in));
    in = Base(); in.hasTarget = false; assert(!ShouldReposition(in));
    // Out of a fight: only when the target fights.
    in = Base(); in.inCombat = false; assert(ShouldReposition(in));
    in.targetInCombat = false; assert(!ShouldReposition(in));

    // Top-off floor.
    assert(Near(TopOffFloor(0, 85), 85.0f));
    assert(Near(TopOffFloor(0, 95), 90.0f));
    assert(Near(TopOffFloor(70, 85), 70.0f));
    assert(Near(TopOffFloor(150, 85), 100.0f));

    // Clamp.
    assert(Near(Clamp(200.0f, 20.0f, 120.0f), 120.0f));
    assert(Near(Clamp(5.0f, 20.0f, 120.0f), 20.0f));
}
