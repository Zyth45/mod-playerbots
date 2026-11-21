/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAMOVEAUDITRULES_H
#define PLAYERBOTS_COAMOVEAUDITRULES_H

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

inline bool TooFast(float flat, uint32_t dtMs, float speed)
{
    if (!dtMs || Teleported(flat))
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
