/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_WORLD_GUILDS_H
#define COA_WORLD_GUILDS_H

#include "Define.h"

#include <string>
#include <vector>

class Player;

// For PlayerbotGuildMgr::AssignToGuild: may a new random-bot guild be made for this side (0 Alliance,
// 1 Horde), given the bot guilds each side has? With AiPlayerbot.CoaWorld.Guilds.Enable or .Balance
// off: always true.
bool CoaWorldGuildSideMayCreate(uint8 side, uint32 allianceGuilds, uint32 hordeGuilds);

// The SquidBots Lite world protocol (CoaWorldAddon.cpp), world thread. On: Guilds.Enable, .Orders and .Addon.
bool CoaWorldGuildAddonOn();
// GORD\t<itemId>\t<qty> (args[0] is the word): GOK\t<id> or ERR\tGORD\t<reason>.
std::vector<std::string> CoaWorldGuildAddonOrder(Player* player, std::vector<std::string> const& args);
// GLIST: the player's GO (7 days) and GG (30 days) lines, newest first.
std::vector<std::string> CoaWorldGuildAddonList(Player* player);

#endif
