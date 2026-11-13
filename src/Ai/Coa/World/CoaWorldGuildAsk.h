/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: a guild for the player who asks (CoaWorldGuildAsk.cpp), shared with the recruiting call of CoaWorldGuilds.

#ifndef PLAYERBOTS_COAWORLDGUILDASK_H
#define PLAYERBOTS_COAWORLDGUILDASK_H

class Guild;
class Player;

namespace CoaWorldGuildAsk
{
// The guild an officer bot may invite a real player into: a bot guild (its leader a random bot), the right to invite,
// a place under the game's own limit. Null otherwise.
Guild* InvitingGuild(Player* bot);
// A bot called for members in General ("/w me"): a whisper to it soon after is an ask for its guild.
void RecruitCall(Player* bot);
}  // namespace CoaWorldGuildAsk

#endif
