/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CoaGardesSettings.h"

#include "CoaGardesConfig.h"

#include <string>

namespace
{
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

CoaGardesSettings const& CoaGardesSettings::Get()
{
    // Published once (thread-safe static initialisation), never written again: no lock on the read.
    static CoaGardesSettings const snapshot = Read();
    return snapshot;
}
