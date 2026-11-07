/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaGardesSettings.h"

#include "CoaGardesConfig.h"
#include "Timer.h"

#include <mutex>
#include <string>

namespace
{
    constexpr uint32 REFRESH_MS = 10000;

    float ReadFloat(char const* key, float fallback)
    {
        std::string const value = CoaConfigString(std::string("AiPlayerbot.") + key, std::string("DungeonClear.") + key,
                                                  std::to_string(fallback));
        try
        {
            return std::stof(value);
        }
        catch (...)
        {
            return fallback;
        }
    }

    CoaGardesSettings Read()
    {
        CoaGardesSettings s;
        s.combatRegroup = CoaConfigBool("AiPlayerbot.CombatRegroup", "DungeonClear.CombatRegroup", s.combatRegroup);
        s.combatRegroupDistance = ReadFloat("CombatRegroupDistance", s.combatRegroupDistance);
        s.combatRegroupSlack = ReadFloat("CombatRegroupSlack", s.combatRegroupSlack);
        s.combatRegroupCooldown = ReadFloat("CombatRegroupCooldown", s.combatRegroupCooldown);
        s.healRepositionHpFloor = ReadFloat("HealRepositionHpFloor", s.healRepositionHpFloor);
        s.roomAggroPathPadding = ReadFloat("RoomAggroPathPadding", s.roomAggroPathPadding);
        s.aggroRangeMargin = ReadFloat("AggroRangeMargin", s.aggroRangeMargin);
        return s;
    }
}

CoaGardesSettings CoaGardesSettings::Get()
{
    static std::mutex lock;
    static CoaGardesSettings cached;
    static uint32 stamp = 0;

    std::lock_guard<std::mutex> guard(lock);
    uint32 const now = getMSTime();
    if (stamp == 0 || getMSTimeDiff(stamp, now) >= REFRESH_MS)
    {
        cached = Read();
        stamp = now ? now : 1;
    }
    return cached;
}
