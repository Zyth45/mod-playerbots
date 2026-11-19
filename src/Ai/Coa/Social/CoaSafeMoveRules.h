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

inline bool CityPathChecked(bool randomBot, bool realMaster, bool inCombat, bool generatePath, bool capital,
                            bool normalPriority)
{
    return randomBot && !realMaster && !inCombat && generatePath && capital && normalPriority;
}
}  // namespace CoaSafeMove

#endif
