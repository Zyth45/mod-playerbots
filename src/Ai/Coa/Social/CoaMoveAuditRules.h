/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAMOVEAUDITRULES_H
#define PLAYERBOTS_COAMOVEAUDITRULES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// CoA 3.1.7 (AiPlayerbot.CoaMoveAudit, off by default): a diagnostic of the bots' odd moves (in the air, through a
// wall, too fast, a jump in height). Two samples of a bot are compared; nothing is changed in the game. Pure rules.
namespace CoaMoveAuditRules
{
constexpr float AIR_HEIGHT = 2.5f;       // above the floor under it (terrain or model), not flying, falling or swimming
constexpr float ROOF_HEIGHT = 2.5f;      // outdoors, the floor it stands on this far above the terrain: a roof
constexpr float TELEPORT_DIST = 60.0f;   // further in one sample: a teleport, not a move
constexpr float WALL_MIN = 0.5f;         // a segment this short is not checked for a wall
constexpr float JUMP_HEIGHT = 2.0f;      // a change of height between two samples on the ground
constexpr float JUMP_MAX_FLAT = 3.0f;    // ... over this little ground
constexpr float FAST_FACTOR = 1.5f;      // this many times its speed
constexpr float FAST_MARGIN = 1.5f;      // plus this, yards per second

inline bool InAir(float z, float floorZ, bool exempt, float airHeight = AIR_HEIGHT)
{
    return !exempt && floorZ > -100000.0f && z - floorZ > airHeight;
}

inline bool OnRoof(bool outdoors, float floorZ, float gridZ)
{
    return outdoors && floorZ > -100000.0f && gridZ > -100000.0f && floorZ - gridZ > ROOF_HEIGHT;
}

inline bool Teleported(float flat) { return flat > TELEPORT_DIST; }

// 3.2.2 (bench of d061adec: 15 800 of 17 500 "teleport" lines were jumps under 500 yards between two samples taken
// seconds apart, a bot whose AI ticks seldom): a teleport is further than the bot could have run in the time, plus
// TELEPORT_DIST.
inline bool TeleportedFor(float flat, uint32_t dtMs, float speed)
{
    return Teleported(flat) && flat > speed * FAST_FACTOR * (float(dtMs) / 1000.0f) + TELEPORT_DIST;
}

inline bool TooFast(float flat, uint32_t dtMs, float speed)
{
    if (!dtMs || TeleportedFor(flat, dtMs, speed))
        return false;
    return flat / (float(dtMs) / 1000.0f) > speed * FAST_FACTOR + FAST_MARGIN;
}

inline bool CheckWall(float flat) { return flat >= WALL_MIN && !Teleported(flat); }

inline bool HeightJump(float dz, float flat, bool bothOnGround)
{
    return bothOnGround && std::fabs(dz) > JUMP_HEIGHT && flat < JUMP_MAX_FLAT;
}

// One line per bot and per kind every throttle: true when this one is written.
// 3.2, a bot going back and forth (the Thousand Needles canyon): over a window of PING_WINDOW_MS out of combat, it
// walked at least PING_TRAVEL yards but never got further than PING_SPAN from where the window began (and moved at
// least PING_MIN_SPAN, not a bot turning on the spot).
constexpr uint64_t PING_WINDOW_MS = 180000;
constexpr float PING_TRAVEL = 150.0f;
constexpr float PING_SPAN = 50.0f;
constexpr float PING_MIN_SPAN = 8.0f;

inline bool PingPong(float traveled, float span)
{
    return traveled >= PING_TRAVEL && span <= PING_SPAN && span >= PING_MIN_SPAN;
}

// 3.2.2 (the user, 23/11: "bots that go through mountains, fly or are in walls"). "terrain": between two samples
// outdoors, both on the terrain's surface, the terrain somewhere in between rises TERRAIN_MARGIN yards over both ends
// (the walls check reads the models only, never the terrain). TERRAIN_STEPS points are read in between.
constexpr float TERRAIN_MARGIN = 2.0f;
constexpr float TERRAIN_SURFACE = 1.0f;  // an end this far under the terrain is in a cave or a city below: not judged
constexpr uint32_t TERRAIN_STEPS = 4;
// 3.2.2: a longer segment between two samples is a path around the hill, not through it (the bench's "terrain" lines:
// 14 to 41 yards between samples, along the navmesh).
constexpr float TERRAIN_MAX_FLAT = 8.0f;

inline bool ThroughTerrain(float zFrom, float gridFrom, float zTo, float gridTo, float highestGridBetween)
{
    if (gridFrom <= -100000.0f || gridTo <= -100000.0f || highestGridBetween <= -100000.0f)
        return false;
    if (zFrom < gridFrom - TERRAIN_SURFACE || zTo < gridTo - TERRAIN_SURFACE)
        return false;
    return highestGridBetween > std::max(zFrom, zTo) + TERRAIN_MARGIN;
}

// 3.2.2: "inwall": standing on the ground, the bot's head sees out in at most INWALL_MAX_OPEN of INWALL_DIRECTIONS
// directions at INWALL_REACH yards: it stands inside a wall or a building's model. Read every INWALL_EVERY_MS.
constexpr uint32_t INWALL_DIRECTIONS = 4;
constexpr uint32_t INWALL_MAX_OPEN = 1;
constexpr float INWALL_REACH = 1.0f;
constexpr uint32_t INWALL_EVERY_MS = 3000;

inline bool InWall(uint32_t openDirections) { return openDirections <= INWALL_MAX_OPEN; }

// 3.2.2 (Pilote): "underfloor": in a capital, on the ground, a floor of the vmap UNDERFLOOR_MIN to UNDERFLOOR_MAX yards
// over the feet (a building's floor over the bot: it stands on the terrain under it).
constexpr float UNDERFLOOR_MIN = 1.0f;
constexpr float UNDERFLOOR_MAX = 6.0f;

// 3.2.2 (bench of 5f662017: 50 lines at Darnassus's bank, bots still on its ground floor, a floor above them): a floor
// over a bot is one it is under only when too low to stand under, UNDERFLOOR_HEADROOM yards at most (Stormwind's bank:
// 1.9); higher, it is the next storey.
constexpr float UNDERFLOOR_HEADROOM = 2.5f;

inline bool UnderFloor(float z, float floorAbove)
{
    return floorAbove > -100000.0f && floorAbove - z >= UNDERFLOOR_MIN && floorAbove - z <= UNDERFLOOR_HEADROOM;
}

// 3.2.2 (same bench): two samples seconds apart in a straight line say nothing of a wall when the bot walked far: it
// went round the corner. Checked over WALL_MAX_FLAT yards at most, as the terrain.
constexpr float WALL_MAX_FLAT = 8.0f;

// 3.2.2 (Pilote): a bot whose feet are within ON_MODEL yards over a model's floor of the vmap stands on a building's
// floor: never "underfloor".
constexpr float ON_MODEL = 0.5f;

inline bool OnModelFloor(float z, float modelZ)
{
    return modelZ > -100000.0f && z - modelZ <= ON_MODEL && z - modelZ >= -ON_MODEL;
}

// 3.2.2: two samples further apart than this (a logout, an AI asleep) are not compared.
constexpr uint64_t SAMPLE_GAP_MS = 30000;

// 3.2.2: a move noted this long before a line is its cause.
constexpr uint64_t CAUSE_MS = 15000;

// 3.2.2: and the SUMMARY_TOP zones and causes with the most lines of each kind.
constexpr size_t SUMMARY_TOP = 5;

// 3.2.2: the hourly summary keeps SUMMARY_SAMPLES samples per kind.
constexpr size_t SUMMARY_SAMPLES = 6;

inline bool Due(uint64_t lastMs, uint64_t nowMs, uint32_t throttleMs) { return !lastMs || nowMs >= lastMs + throttleMs; }

// The core's PathType flags as text, for the report.
inline std::string PathTypeText(uint32_t type)
{
    static char const* const names[] = {"NORMAL", "SHORTCUT", "INCOMPLETE", "NOPATH", "NOT_USING_PATH", "SHORT",
                                        "FARFROMPOLY_START", "FARFROMPOLY_END"};
    if (!type)
        return "BLANK";
    std::string text;
    for (uint32_t bit = 0; bit < 8; ++bit)
        if (type & (1u << bit))
            text += (text.empty() ? "" : "|") + std::string(names[bit]);
    return text;
}

// A string for a JSON value: quotes, backslashes and control characters escaped.
inline std::string Json(std::string const& text)
{
    std::string out = "\"";
    for (char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\', out += c;
        else if (static_cast<unsigned char>(c) < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out + "\"";
}
}  // namespace CoaMoveAuditRules

#endif
