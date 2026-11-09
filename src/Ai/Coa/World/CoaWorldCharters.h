/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: guild charters (CoaWorldCharters.cpp).

#ifndef PLAYERBOTS_COAWORLDCHARTERS_H
#define PLAYERBOTS_COAWORLDCHARTERS_H

#include "ObjectGuid.h"

class Player;

// For PetitionSignAction: a real player offers its charter to this bot. True when CoA World takes it (a guild charter,
// a random bot of its own without a guild: it signs after a few seconds, or refuses now and then); false: the usual
// rules, security check included. With AiPlayerbot.CoaWorld.Guilds.Charters off: always false.
bool CoaWorldCharterOffered(Player* bot, ObjectGuid petition, Player* owner, bool guildCharter);

// For PlayerbotGuildMgr::AssignToGuild: may a random bot join a bot guild (Guilds.UnguildedPercent floor)?
// With Charters off: always true.
bool CoaWorldGuildMayJoin();

void AddSC_coa_world_charters();

#endif
