/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
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
