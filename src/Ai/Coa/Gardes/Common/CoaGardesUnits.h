/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COA_GARDES_UNITS_H
#define COA_GARDES_UNITS_H

#include "CoaGroupTank.h"
#include "Player.h"
#include "Playerbots.h"

// The live reads every party combat guard shares.
namespace CoaGardesUnits
{
    // The group's reference tank, read fresh (no cached pointer): alive and on the bot's map, or nullptr.
    inline Player* Tank(Player* bot)
    {
        return bot ? CoaGroupTank::Reference(bot) : nullptr;
    }

    // A tank no PlayerbotAI drives: a human Main Tank (Dungeon Clear's DcPlayerLed::IsHumanTank).
    inline bool IsHuman(Player* tank)
    {
        return tank && !GET_PLAYERBOT_AI(tank);
    }

    inline bool Controlled(Player* bot)
    {
        return bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED | UNIT_STATE_ROOT);
    }
}

#endif
