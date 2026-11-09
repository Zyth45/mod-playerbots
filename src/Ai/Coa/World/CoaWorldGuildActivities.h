/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: guild activities from the addon (CoaWorldGuildActivities.cpp), world thread.

#ifndef PLAYERBOTS_COAWORLDGUILDACTIVITIES_H
#define PLAYERBOTS_COAWORLDGUILDACTIVITIES_H

#include <string>
#include <vector>

class Player;

// Guilds.Enable, Guilds.Activities and Guilds.Addon.
bool CoaWorldGuildActivitiesOn();
// GACT?: one GA line per kind.
std::vector<std::string> CoaWorldGuildActivityList(Player* player);
// GACT <kind> or GACT cancel (args[0] is the word): the GA lines, or ERR GACT <reason>.
std::vector<std::string> CoaWorldGuildActivityStart(Player* player, std::vector<std::string> const& args);

void AddSC_coa_world_guild_activities();

#endif
