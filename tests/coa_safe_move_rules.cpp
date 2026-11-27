#include "CoaSafeMoveRules.h"

#include <cassert>
#include <cmath>
#include <string>

int main()
{
    using namespace CoaSafeMove;

    // A whole path on the mesh; the bot a little off it; a long path cut short.
    assert(Walkable(NORMAL, false) && Walkable(NORMAL | FARFROMPOLY_START, false) && Walkable(NORMAL | SHORT, false));
    // The point off the mesh (in a building), a hole, a partial path, no path: no move.
    assert(!Walkable(NORMAL | FARFROMPOLY_END, false) && !Walkable(NORMAL | SHORTCUT, false));
    assert(!Walkable(INCOMPLETE, false) && !Walkable(NORMAL | INCOMPLETE, false) && !Walkable(NOPATH, false));
    assert(!Walkable(0, false) && !Walkable(SHORTCUT, true));
    // A straight line: only a swim toward a shore in sight.
    assert(!Walkable(NORMAL | NOT_USING_PATH, false) && Walkable(NORMAL | NOT_USING_PATH, true));
    assert(!Walkable(NOPATH | NOT_USING_PATH, true));
    // 3.1.6 trial: a random bot of its own on a normal move in a capital goes only where a whole path leads.
    // Only the core's straight line is refused: a long path cut at the point limit (INCOMPLETE) is followed. Refusing
    // it trapped bots in the Stormwind bank (lot 4 trial).
    assert(CoreStraightLine(NOPATH, 0) && CoreStraightLine(NOPATH | INCOMPLETE, 40));
    assert(!CoreStraightLine(NORMAL | INCOMPLETE, 74));                 // long, cut: followed
    assert(!CoreStraightLine(NORMAL | FARFROMPOLY_END, 12));
    assert(!CoreStraightLine(NORMAL | FARFROMPOLY_START, 12));
    assert(!CoreStraightLine(NORMAL, 2));                               // a short straight walk on the mesh
    assert(CoreStraightLine(NORMAL | INCOMPLETE, 2));                   // two points, cut: the line through the wall
    assert(CoreStraightLine(SHORTCUT, 2) && CoreStraightLine(NORMAL | NOT_USING_PATH, 2));
    // 3.1.8: a still bot off a taxi falls to its floor (Morgan's Vigil: 188.07 over 185.18), only in that case.
    assert(DropsToFloor(188.07f, 185.18f, true, false));
    assert(!DropsToFloor(188.07f, 185.18f, false, false));   // indoors (a floor of a building): left alone
    assert(!DropsToFloor(188.07f, 185.18f, true, true));     // water under it
    assert(!DropsToFloor(188.07f, -200000.0f, true, false)); // no floor found
    assert(!DropsToFloor(185.9f, 185.18f, true, false));     // on its floor (a platform, a tower: its own floor)
    assert(!DropsToFloor(204.0f, 185.18f, true, false));     // too high to be that case: left alone
    // 3.1.8: a teleport to a banker arrives outdoors around the bank, on ground near the banker's floor.
    assert(ArrivalOk(98.0f, 99.7f, true, false, 97.9f));
    assert(!ArrivalOk(98.0f, 99.7f, false, false, 97.9f));   // inside a building
    assert(!ArrivalOk(98.0f, 99.7f, true, true, 97.9f));     // in water (the canals)
    assert(!ArrivalOk(110.0f, 99.7f, true, false, 97.9f));   // a roof or a terrace far above
    assert(!ArrivalOk(-200000.0f, 99.7f, true, false, 97.9f));
    assert(!ArrivalOk(103.6f, 99.7f, true, false, 98.9f));  // the roof by the bank door (bench of 20/11)
    // 3.2: the ledge by the Stormwind bank, a short cut path (20 points) ending 28 yards from the NPC: a dead end.
    assert(DeadEndPath(NORMAL | INCOMPLETE, 20, 28.0f));
    assert(!DeadEndPath(NORMAL | INCOMPLETE, 74, 28.0f));  // cut by the point limit: goes on from its end
    assert(!DeadEndPath(NORMAL | INCOMPLETE, 20, 2.0f));   // ends by the target
    assert(!DeadEndPath(NORMAL, 20, 28.0f));               // a whole path
    assert(CityPathChecked(true, false, false, true, true, true));
    assert(!CityPathChecked(false, false, false, true, true, true));  // a player's alt: left alone
    assert(!CityPathChecked(true, true, false, true, true, true));    // in a real player's group
    assert(!CityPathChecked(true, false, true, true, true, true));    // in a fight
    assert(!CityPathChecked(true, false, false, false, true, true));  // flying or swimming
    assert(!CityPathChecked(true, false, false, true, false, true));  // out of a capital
    assert(!CityPathChecked(true, false, false, true, true, false));  // a forced move (flee)
    // The path types of the trial: no path or a cut path is never walked (the straight line through the wall).
    assert(!Walkable(NOPATH, false) && !Walkable(NORMAL | INCOMPLETE, false) && !Walkable(SHORTCUT, false));
    // 3.2: a stop by an NPC behind its counter.
    assert(StandsByNpc(3.0f));
    assert(StandsByNpc(4.5f));
    assert(!StandsByNpc(6.0f));
    assert(WANDER_GIVE_UPS >= 2);
    // 3.2: a refused move is not a refused way.
    assert(GivesUpNpc(true, true, 0) == GiveUp::Gone);
    assert(GivesUpNpc(false, false, 1) == GiveUp::NoWay);
    assert(GivesUpNpc(false, true, 1) == GiveUp::No);
    assert(GivesUpNpc(false, true, WANDER_REFUSALS) == GiveUp::Refusals);
    assert(std::string(NoWayReason(INCOMPLETE | FARFROMPOLY_START, 10, false, true)) == "bot off the mesh");
    assert(std::string(NoWayReason(INCOMPLETE, 10, false, true)) == "dead end");
    assert(std::string(NoWayReason(NORMAL, 10, true, false)) == "out of sight");
    // 3.2, the living bench's cases. Undercity: 46.2 yards to Auctioneer Yarly over the flat ring (1.4 yards lower).
    assert(FlatLineArrives(46.2f, 1.4f, true));
    assert(!FlatLineArrives(46.2f, 1.4f, false));   // behind a wall
    assert(!FlatLineArrives(46.2f, 4.0f, true));    // over a canal or a ledge
    assert(!FlatLineArrives(80.0f, 0.0f, true));    // too far for a line
    // Stormwind (the path to Chilton stops 16.1 yards short): the bridge rule, kept for 3.2.1 and unused in 3.2.
    assert(BridgesDeadEnd(16.1f, true));
    assert(!BridgesDeadEnd(16.1f, false));           // a wall or a drop in between
    assert(!BridgesDeadEnd(25.0f, true));            // too far short
    assert(!StandsByNpc(16.1f));                     // what refused it before
    // 3.2.2: off a closed island: a walk first, a teleport only unseen, else the visit given up.
    assert(UnstickChoice(false, false, false) == Unstick::NotStuck);
    assert(UnstickChoice(true, true, true) == Unstick::Walk);
    assert(UnstickChoice(true, false, false) == Unstick::Teleport);
    assert(UnstickChoice(true, false, true) == Unstick::Abandon);
    // 3.2.2: the start cells of the ring's misses: 4 yards a side, the two Stormwind bank arrivals apart.
    assert(StartCell(-8919.9f, 634.9f) == StartCell(-8918.5f, 633.2f));
    assert(StartCell(-8919.9f, 634.9f) != StartCell(-8893.8f, 643.9f));
    // 3.2.2: Chilton at 97.47 over the city's floor at 95.90: the dropped point is within the climb of that floor only.
    assert(97.47f - STAND_RING_DROP - 95.90f < 0.1f);
    // 3.2.2: the trade square's floor at 94.5, 3 yards under Chilton: the second drop is within the climb of it.
    assert(std::fabs(97.47f - STAND_RING_DROP2 - 94.5f) < 1.6f);
    // 3.2.2: Undercity's Epitwee 5.3 yards over the floor: a drop within the climb of it, and a stop 5.5 yards off in
    // 3D within the core's reach (5.5 plus the sizes), not within NPC_STAND_REACH.
    {
        bool within = false;
        for (uint32_t d = 0; d < STAND_RING_DROP_COUNT; ++d)
            within |= std::fabs(-56.79f - STAND_RING_DROPS[d] - (-62.11f)) < 1.6f;
        assert(within);
    }
    assert(InInteractionReach(5.5f, 0.5f, 0.4f) && !StandsByNpc(5.5f));
    assert(!InInteractionReach(7.42f, 0.5f, 0.4f));
    // 3.2.2: the Ironforge bank: the user's spots 4.4 yards in front of banker 2461 (facing 2.23); its booth behind.
    assert(InFrontOf(-4886.55f, -997.594f, 2.23402f, -4889.0996f, -994.0582f));
    assert(!InFrontOf(-4886.55f, -997.594f, 2.23402f, -4884.0f, -1001.0f));   // behind the banker
    assert(!InFrontOf(-4886.55f, -997.594f, 2.23402f, -4886.0f, -997.0f));    // beside: under a yard out
    // 3.2.2: an auctioneer on a dais behind its counter: seen over it, or the stop at the counter.
    assert(SeenOrAtCounter(false, true, 6.0f, 0.0f));
    assert(SeenOrAtCounter(false, false, 4.46f, -1.2f));   // Ironforge's auctioneer 9859, on its dais
    assert(!SeenOrAtCounter(false, false, 6.03f, 0.0f));   // out of reach
    assert(!SeenOrAtCounter(false, false, 4.0f, -3.5f));   // another floor, under the NPC
    assert(!SeenOrAtCounter(false, false, 4.0f, 3.0f));    // another floor, over it
    assert(UNSTICK_RADII[2] <= UNSTICK_REACH);
    // 3.2.2: the ring around an NPC, first toward the bot, then alternating sides, all within reach of it.
    {
        float radius = 0.0f, angle = 0.0f;
        StandRingPoint(0, 1.0f, radius, angle);
        assert(radius == 2.0f && angle == 1.0f);
        StandRingPoint(1, 1.0f, radius, angle);
        assert(angle > 1.0f);
        StandRingPoint(2, 1.0f, radius, angle);
        assert(angle < 1.0f);
        StandRingPoint(STAND_RING_TRIES - 1, 1.0f, radius, angle);
        assert(radius == 3.5f && StandsByNpc(radius));
    }
    // 3.2.2: the Stormwind bank: its arrival on the ledge right of the stairs (-8893.8 643.9 98.3), the bench and the
    // ledge go to the hall's floor; the hall's floor itself and the bank's other side stay as they are.
    {
        float fx = 0.0f, fy = 0.0f, fz = 0.0f;
        assert(FixedArrival(0, -8893.8f, 643.9f, 98.3f, fx, fy, fz) && fx == -8897.6f && fy == 640.0f && fz == 99.7f);
        assert(FixedArrival(0, -8895.2f, 642.5f, 100.7f, fx, fy, fz) && FixedArrival(0, -8889.8f, 643.9f, 100.4f, fx, fy, fz));
        assert(!FixedArrival(0, -8897.6f, 640.0f, 99.7f, fx, fy, fz));     // the spot is out of its own zone
        assert(!FixedArrival(0, -8914.2f, 629.6f, 99.7f, fx, fy, fz));
        assert(!FixedArrival(1, -8893.8f, 643.9f, 98.3f, fx, fy, fz));     // another map
        assert(!FixedArrival(0, -8893.8f, 643.9f, 110.0f, fx, fy, fz));    // another floor
        assert(Avoided(0, -8893.8f, 643.9f, 98.3f) && !Avoided(0, -8897.6f, 640.0f, 99.7f));
    }
    // 3.2.2: the Stormwind inn: a step aimed at the navmesh's 97.9 where the server's floor is 96.1 is refused; a bot at
    // 96.1 under the navmesh's 97.9 is in the floor; a bot standing on its floor (0.3 yard of mesh error) is not.
    assert(!CityFloorAgrees(97.9f, 96.1f) && !CityFloorAgrees(97.9f, 97.1f) && CityFloorAgrees(97.9f, 97.5f));
    assert(BuriedUnder(96.1f, 97.9f) && BuriedUnder(97.1f, 97.9f) && !BuriedUnder(97.6f, 97.9f) && !BuriedUnder(98.5f, 97.9f));
    assert(StillGain(1000) == 1000 && StillGain(STILL_SLEPT_MS + 1) == 0);
    assert(UnstickAllowed(0, 0, 0) && !UnstickAllowed(5, 1000, 0) && UnstickAllowed(5, UNSTICK_EVERY_MS, 0));
    assert(!UnstickAllowed(0, 0, UNSTICK_PER_HOUR));
    // 3.2.3: no wander to an NPC that walks, nor to a festival commoner (18927 Human ... 20102 Goblin).
    assert(WanderNpcRefused(18927, false) && WanderNpcRefused(19176, false) && WanderNpcRefused(20102, false));
    assert(WanderNpcRefused(2455, true));    // any NPC on waypoints or random moves
    assert(!WanderNpcRefused(2455, false));  // a banker standing at its counter
    // 3.2.3: Tal 2995 is stopped by on his side, 1.8 yd square to his facing; his own spot is out of the void's disc.
    assert(SideStopNpc(2995) && !SideStopNpc(2996));
    float sx = 0.0f, sy = 0.0f;
    SideStopPoint(0.0f, 0.0f, 0.0f, 0, sx, sy);
    assert(std::fabs(sx) < 0.01f && std::fabs(sy - SIDE_STOP) < 0.01f);
    SideStopPoint(0.0f, 0.0f, 0.0f, 1, sx, sy);
    assert(std::fabs(sx) < 0.01f && std::fabs(sy + SIDE_STOP) < 0.01f);
    assert(SideStopFloor(177.0f, 177.6f) && !SideStopFloor(177.0f, 174.0f));
    assert(!Avoided(1, -1196.75f, 26.08f, 177.6f) && Avoided(1, -1197.6f, 32.0f, 177.5f));
    assert(Avoided(1, 9862.49f, 2342.75f, 1327.3f) && Avoided(1, 9863.5f, 2345.68f, 1327.3f));  // Darnassus
    // 3.2.3: indoors, off the mount, walking or not; never in flight, on a boat or in a battleground.
    assert(IndoorDismount(true, false, false, false, false));
    assert(!IndoorDismount(true, true, false, false, false));
    assert(!IndoorDismount(false, false, false, false, false));
    assert(!IndoorDismount(true, false, true, false, false) && !IndoorDismount(true, false, false, true, false));
    assert(!IndoorDismount(true, false, false, false, true));
    // 3.2.3: stops by an NPC aside, 45-75 degrees off its facing, both sides, 2 yd at least off its axis.
    for (uint64_t g = 0; g < 16; ++g)
    {
        float const off = AsideOffset(g);
        float const deg = std::fabs(off) / 0.017453293f;
        assert(deg >= 44.9f && deg <= 75.1f);
        float const d = AsideDistance(1.5f, 0.4f, off, 8.0f);
        assert(d * std::fabs(std::sin(off)) >= ASIDE_LATERAL - 0.01f && d <= 8.0f);
    }
    assert(AsideOffset(0) > 0.0f && AsideOffset(1) < 0.0f);
    assert(AsideDistance(1.5f, 1.5f, AsideOffset(6), 8.0f) >= 3.5f - 0.01f);  // a tauren: reach plus its size
    assert(AsideDistance(1.5f, 1.5f, AsideOffset(0), 3.0f) == 3.0f);         // never past the wanted distance
    return 0;
}
