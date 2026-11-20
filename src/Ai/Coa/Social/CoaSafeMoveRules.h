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

inline bool ArrivalOk(float groundZ, float bankerZ, bool outdoors, bool water)
{
    return groundZ > -100000.0f && outdoors && !water && std::fabs(groundZ - bankerZ) <= ARRIVAL_MAX_DZ;
}

inline bool CityPathChecked(bool randomBot, bool realMaster, bool inCombat, bool generatePath, bool capital,
                            bool normalPriority)
{
    return randomBot && !realMaster && !inCombat && generatePath && capital && normalPriority;
}
}  // namespace CoaSafeMove

#endif
