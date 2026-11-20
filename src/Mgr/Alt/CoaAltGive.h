/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTGIVE_H
#define PLAYERBOTS_COAALTGIVE_H

#include <string>

class PlayerbotAI;

// CoA 3.1.6, "Give to alt" of the SquidBots addon (AiPlayerbot.CoaGiveToAlt.Enable): an alt of a player gives an
// item of its bags to another alt of the same player, from bag to bag. "give <itemId> <count> <alt>" whispered on
// SQBA to the alt that carries it; both alts in their master's group, never a recruited bot. The items not bound
// (bound to the account is fine) of that id are taken wherever they lie, stacks split or gathered up to count.
// Answers "GIVE\tdone\t<itemId>\t<count>\t<alt>" or "GIVE\terr\t<itemId>\t<reason>", reason one of bound, notalt,
// notgroup, full, missing, off. Never a part: fewer than count, nothing moves (missing).
// The items leave the carrier at once and reach the other alt on its own update (its map thread); an alt gone
// offline in between gets them by mail.
namespace CoaAltGive
{
// The command of the queue (CoaAltBags::Command): true when it was "give".
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);
// The bot's update: items given to it are stored, its own parcels left unclaimed for 10 s go by mail.
void Tick(PlayerbotAI* botAI);
}  // namespace CoaAltGive

#endif
