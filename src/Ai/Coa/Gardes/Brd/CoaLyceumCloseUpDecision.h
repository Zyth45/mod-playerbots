/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LYCEUM_CLOSE_UP_DECISION_H
#define COA_LYCEUM_CLOSE_UP_DECISION_H

#include "CoaBrdData.h"

#include <cmath>

// Blackrock Depths' Lyceum close-up, from Dungeon Clear (DcLyceumCloseUpDecision.h), pure and unchanged: in a fight
// inside the room every member stands within LEASH of the tank, the way a real group plays a room of 232 Anvilrage
// Reservists that refill a minute after they die. Two halves, one radius:
//   * Decide: a member further than LEASH from the tank walks back to CLOSE_TO of it (the two radii are the
//     hysteresis);
//   * ChaseLeavesThePack: stock reach / chase toward a target further than LEASH from the tank is zeroed, so the stray
//     comes to the pack instead of a melee ping-ponging between it and the tank.
namespace CoaLyceumCloseUp
{
    constexpr float LEASH = 15.0f;
    constexpr float CLOSE_TO = 9.0f;
    constexpr float AIM = CLOSE_TO - 2.0f;

    inline bool InRoom(float x, float y, float z)
    {
        using namespace CoaBrd;
        return x >= LYCEUM_BOX_X_MIN && x <= LYCEUM_BOX_X_MAX && y >= LYCEUM_BOX_Y_MIN && y <= LYCEUM_BOX_Y_MAX &&
               z >= LYCEUM_BOX_Z_MIN && z <= LYCEUM_BOX_Z_MAX;
    }

    inline float Dist2d(float ax, float ay, float bx, float by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    struct Verdict
    {
        bool close = false;
        float x = 0.0f;
        float y = 0.0f;
    };

    // `closing`: the member is already walking back (the latch); it keeps walking until inside CLOSE_TO. The aim point
    // is on the member's own side of the tank, so a healer behind the tank stays behind it.
    inline Verdict Decide(float botX, float botY, float tankX, float tankY, bool closing)
    {
        Verdict v;
        float const d = Dist2d(botX, botY, tankX, tankY);
        if (d <= (closing ? CLOSE_TO : LEASH) || d <= 0.01f)
            return v;
        v.close = true;
        v.x = tankX + (botX - tankX) / d * AIM;
        v.y = tankY + (botY - tankY) / d * AIM;
        return v;
    }

    inline bool ChaseLeavesThePack(float targetX, float targetY, float tankX, float tankY)
    {
        return Dist2d(targetX, targetY, tankX, tankY) > LEASH;
    }

    // Whether the close-up applies to this member (both halves). The reference tank itself never closes up on
    // anyone; a bot tank leaves a HUMAN reference tank alone; the tank must be alive and in the same instance, and
    // both must stand in the room.
    struct Gate
    {
        bool hasTank = false;
        bool tankIsSelf = false;
        bool tankAlive = true;
        bool sameInstance = true;
        bool tankIsHuman = false;
        bool botIsTank = false;
        bool botInRoom = false;
        bool tankInRoom = false;
    };

    constexpr bool Applies(Gate const& g)
    {
        return g.hasTank && !g.tankIsSelf && g.tankAlive && g.sameInstance && !(g.tankIsHuman && g.botIsTank) &&
               g.botInRoom && g.tankInRoom;
    }
}

#endif
