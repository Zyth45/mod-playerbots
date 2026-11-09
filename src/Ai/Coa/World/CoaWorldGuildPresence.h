/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: the bot members of a real player's guild online with that player (CoaWorldGuildPresence.cpp).

#ifndef PLAYERBOTS_COAWORLDGUILDPRESENCE_H
#define PLAYERBOTS_COAWORLDGUILDPRESENCE_H

#include "Define.h"

#include <vector>

class Player;

// For RandomPlayerbotMgr (rotation, world thread): puts first the accounts of the offline bot members of guilds that
// have a real member online, up to PlayerGuildOnlinePercent of each guild's bots. Off: nothing changes.
void CoaWorldGuildPresenceOrderAccounts(std::vector<uint32>& accounts);

// For RandomPlayerbotMgr::ProcessBot: this bot stays in the world when its time is up (its guild has a real member
// online and no more than its share of bots online). Off: false.
bool CoaWorldGuildPresenceKeep(Player* bot);

void AddSC_coa_world_guild_presence();

#endif
