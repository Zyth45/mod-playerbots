/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASTARTERWEAPON_H
#define PLAYERBOTS_COASTARTERWEAPON_H

class Player;

// CoA 3.1.6 (AiPlayerbot.CoaSelfFound.StarterWeapon, CoaStarterWeaponRules): the starting weapon of its class, once,
// for a random bot of a self-found realm left with no weapon at all. The bots that got it are kept in the table
// playerbots_coa_starter_weapon, so a bot that sells or destroys it never gets it again, across restarts too.
namespace CoaStarterWeapon
{
// Loads the table (world start).
void Load();
// Checks the bot and gives the weapon when the rule says so (map or world thread of the bot). True when given.
bool Ensure(Player* bot);
}  // namespace CoaStarterWeapon

#endif
