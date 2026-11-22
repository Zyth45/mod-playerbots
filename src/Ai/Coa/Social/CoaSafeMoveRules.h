/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COASAFEMOVERULES_H
#define _PLAYERBOT_COASAFEMOVERULES_H

// A point move of the CoA code (city life, gathering, revive spot, out of water) goes only where the navmesh leads:
// the core moves a unit in a straight line, through walls and hills, when the path to the point is not found (a
// point inside a building, under a counter, off the mesh). Without the game objects, for the unit test.

#include <cmath>
#include <cstdint>

namespace CoaSafeMove
{
// The core's PathType flags (PathGenerator.h; checked against it in CoaSafeMove.cpp).
constexpr uint32_t NORMAL = 0x01, SHORTCUT = 0x02, INCOMPLETE = 0x04, NOPATH = 0x08, NOT_USING_PATH = 0x10,
                   SHORT = 0x20, FARFROMPOLY_START = 0x40, FARFROMPOLY_END = 0x80;

// A path the bot may walk: a whole path on the mesh to a point on it. The bot itself a little off the mesh is fine
// (FARFROMPOLY_START), a path cut short (SHORT, long trips) too. A straight line (NOT_USING_PATH: no mmap, or
// swimming) only for a swim in sight of the shore (waterLine: in water, the point in line of sight).
inline bool Walkable(uint32_t type, bool waterLine)
{
    if (!(type & NORMAL) || (type & (SHORTCUT | INCOMPLETE | NOPATH | FARFROMPOLY_END)))
        return false;
    return !(type & NOT_USING_PATH) || waterLine;
}
// 3.1.6 trial: in a capital, a random bot walked through walls. The generic move asks the core for a path, and the
// core goes in a straight line when it finds none (no path, or a path of two points). A random bot of its own (no
// real player's group), out of combat, on a normal move in a capital goes only where a whole path leads.
// What the core does with the path of a point move (PointMovementGenerator): it follows any path of more than two
// points that is not NOPATH (a cut path too: INCOMPLETE, the long ones cut at the point limit, or off the mesh at an
// end), and goes in a straight line otherwise. The straight line is the only thing refused in a capital; a path of two
// points is a straight line too, walkable only when it is a whole path on the mesh.
inline bool CoreStraightLine(uint32_t type, uint32_t points)
{
    if (type & NOPATH)
        return true;
    return points <= 2 && (!(type & NORMAL) || (type & (SHORTCUT | INCOMPLETE | NOT_USING_PATH | FARFROMPOLY_START |
                                                        FARFROMPOLY_END)));
}

// 3.1.8 (move audit of 20/11): a bot off a taxi stands at the height of the path's last node, up to a few yards over
// the ground (Morgan's Vigil: 2.9 yards), and the server never lets a still bot fall: it hung in the air for minutes.
// Dropped to its floor only in that exact case: outdoors (a platform or a tower has its own floor under the bot, an
// indoor floor is left alone), no water under it, a floor found, and between DROP_MIN and DROP_MAX above it.
constexpr float DROP_MIN = 1.5f;
constexpr float DROP_MAX = 8.0f;

inline bool DropsToFloor(float z, float floorZ, bool outdoors, bool waterBelow)
{
    if (floorZ <= -100000.0f || !outdoors || waterBelow)
        return false;
    float const above = z - floorZ;
    return above > DROP_MIN && above <= DROP_MAX;
}

// 3.1.8 (move audit of 20/11): a quarter of the level teleports go to a banker (ProbTeleToBankers), its position
// inside the bank, with the bot's home set there: 49 teleports of 44 bots into the Stormwind bank in 25 minutes. The
// arrival is now the first point outdoors around the banker, ring after ring (ARRIVAL_RINGS yards, 16 directions),
// on ground no more than ARRIVAL_MAX_DZ from the banker's floor; none found: the banker's spot, as before.
constexpr float ARRIVAL_RINGS[] = {10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f};
constexpr uint32_t ARRIVAL_DIRECTIONS = 16;
constexpr float ARRIVAL_MAX_DZ = 6.0f;
// The bench after the fix (20/11): one arrival of the Stormwind bank was a roof by the door, 4.7 yards over the
// terrain. The arrival stands on the terrain itself: its ground no more than ARRIVAL_ON_TERRAIN over the map's grid.
constexpr float ARRIVAL_ON_TERRAIN = 1.0f;

inline bool ArrivalOk(float groundZ, float bankerZ, bool outdoors, bool water, float gridZ)
{
    return groundZ > -100000.0f && gridZ > -100000.0f && outdoors && !water &&
           std::fabs(groundZ - bankerZ) <= ARRIVAL_MAX_DZ && groundZ - gridZ <= ARRIVAL_ON_TERRAIN;
}

// 3.2 (the user's report of 20/11, bots standing on a ledge by the Stormwind bank, -8876.0 626.4 95.7): a path the
// core cuts short because the target is out of reach (another island of the mesh) ends on the reachable point
// nearest to it, here a ledge over the street with no way down; the bots walked there and stood. A path cut by the
// point limit (a long one, DEAD_END_LIMIT_POINTS points or more) is still walked: it goes on from its end. A short
// cut path whose end stays DEAD_END_MIN_GAP yards or more from the target is a dead end, refused in a capital.
constexpr uint32_t DEAD_END_LIMIT_POINTS = 70;
constexpr float DEAD_END_MIN_GAP = 5.0f;

inline bool DeadEndPath(uint32_t type, uint32_t points, float endToTarget)
{
    return (type & INCOMPLETE) && points < DEAD_END_LIMIT_POINTS && endToTarget >= DEAD_END_MIN_GAP;
}

// 3.2: an NPC behind a counter (a banker, an auctioneer): the stop picked by it lies over the counter, off the
// navmesh, and the path there is a dead end. The end of that path, the walkable point nearest the NPC, is taken
// instead when it is NPC_STAND_REACH yards or less from the NPC (the core lets a player talk from 5.5 yards).
constexpr float NPC_STAND_REACH = 4.5f;
// A wander that gave up this many NPCs in a row (no walkable way) ends: the bot goes on with something else
// instead of standing still and picking again.
constexpr uint32_t WANDER_GIVE_UPS = 3;

// 3.2 (living bench: 118 of 332 auctioneers given up on the way): a refused move is not always a refused way. The
// move layer also refuses a move to the very point of the last one within 5 seconds (StandByNpc's stop is the same
// each time), a bot that cannot move right now, a pull risk. An NPC is given up when it is gone, when no walkable
// stop by it exists, or after WANDER_REFUSALS refused moves toward it.
constexpr uint32_t WANDER_REFUSALS = 8;

enum class GiveUp { No, Gone, NoWay, Refusals };

inline GiveUp GivesUpNpc(bool gone, bool wayExists, uint32_t refusals)
{
    if (gone)
        return GiveUp::Gone;
    if (!wayExists)
        return GiveUp::NoWay;
    return refusals >= WANDER_REFUSALS ? GiveUp::Refusals : GiveUp::No;
}

// 3.2 (living bench, Undercity: a bot 46.2 yards from Auctioneer Yarly, 1.4 yards lower, got a straight line that
// ends 0.1 yard off; it was refused). A straight line in sight whose ground stays within FLAT_LINE_DZ yards of the
// line all along (sampled every FLAT_LINE_STEP yards: no canal, no ledge, no stairs) goes through no wall and over
// no fall. Accepted up to FLAT_LINE_MAX yards.
constexpr float FLAT_LINE_MAX = 60.0f;
constexpr float FLAT_LINE_DZ = 1.5f;
constexpr float FLAT_LINE_STEP = 2.0f;

inline bool FlatLineArrives(float length, float maxGroundGap, bool inSight)
{
    return inSight && length <= FLAT_LINE_MAX && maxGroundGap <= FLAT_LINE_DZ;
}

// 3.2 (living bench, Stormwind: from the street the path to all three auctioneers stops 16.1 yards short, their
// counter is cut off the street's navmesh). A dead end whose end stays BRIDGE_MAX yards or less from the NPC could be
// walked when the rest, from its end to the NPC, is a flat line. Not used in 3.2: the bench's bots stood at the end
// of it (StandByNpc); kept for the 3.2.1 fix (a fixed stop per Stormwind auctioneer).
constexpr float BRIDGE_MAX = 20.0f;

inline bool BridgesDeadEnd(float endToNpc, bool flatRest)
{
    return flatRest && endToNpc <= BRIDGE_MAX;
}

// 3.2 (BuyLog): why no walkable stop by an NPC exists, from the path to it.
inline char const* NoWayReason(uint32_t type, uint32_t points, bool closeEnough, bool inSight)
{
    if (type & FARFROMPOLY_START)
        return "bot off the mesh";
    if (type & NOPATH)
        return "no path";
    if (CoreStraightLine(type, points))
        return "straight line";
    if (!closeEnough)
        return "dead end";
    if (!inSight)
        return "out of sight";
    return "way found";
}

inline bool StandsByNpc(float endToNpc)
{
    return endToNpc <= NPC_STAND_REACH;
}

inline bool CityPathChecked(bool randomBot, bool realMaster, bool inCombat, bool generatePath, bool capital,
                            bool normalPriority)
{
    return randomBot && !realMaster && !inCombat && generatePath && capital && normalPriority;
}
}  // namespace CoaSafeMove

#endif
