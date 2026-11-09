#include "CoaCleanWorldRules.h"

#include <cassert>

using namespace CoaCleanWorld;

int main()
{
    Point const body{100.0f, 100.0f};
    std::vector<Point> const pack{{105.0f, 100.0f}, {95.0f, 104.0f}};  // the two that killed it, on the body

    assert(Unsafe(body, pack));
    assert(!Unsafe(body, {{150.0f, 100.0f}}));
    assert(!Unsafe(body, {}));

    std::vector<Point> const spots = Around(body);
    assert(spots.size() == 8);
    for (Point const& s : spots)
        assert(std::fabs(Dist(s, body) - RECLAIM_RADIUS) < 0.01f);  // within reach of the body

    // The pack on the body: any spot 30 yd away is safe, the farthest from them wins.
    int const pick = PickSafeSpot(spots, pack);
    assert(pick >= 0 && !Unsafe(spots[pick], pack));
    // Monsters all around: no safe spot, the spirit healer.
    std::vector<Point> ring;
    for (Point const& s : spots)
        ring.push_back(s);
    assert(PickSafeSpot(spots, ring) == -1);
    // A camp to the east only: a spot to the west.
    int const west = PickSafeSpot(spots, {{130.0f, 100.0f}, {128.0f, 110.0f}, {128.0f, 90.0f}});
    assert(west >= 0 && spots[west].x < body.x);

    // The water: idle and swimming for 30 s.
    assert(LeavesWater(true, true, false, 30) && !LeavesWater(true, true, false, 29));
    assert(!LeavesWater(true, false, false, 60) && !LeavesWater(true, true, true, 60) && !LeavesWater(false, true, false, 60));
    return 0;
}
