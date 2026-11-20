/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COABOTMUTE_H
#define PLAYERBOTS_COABOTMUTE_H

#include <string>

class Player;
class PlayerbotAI;

// CoA 3.1.6, the Mute button of the SquidBots addon (AiPlayerbot.CoaMute.Enable): a player silences the chatter of
// all his bots at once, alts and recruited, and it stays so between sessions (table playerbots_coa_mute). "mute
// on|off" (or "mute" alone: the state) whispered on SQBA by the master to any of his bots, answered "MUTE\t<0|1>".
// A muted bot says no living chat line (CoA Live, World, Economy), whispers nothing unasked and says no random line;
// it still answers its master's orders and questions.
namespace CoaBotMute
{
// Loads the table (world start).
void Load();
// The command of the queue (CoaAltBags::Command): true when it was "mute".
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);
// The bot's master muted his bots.
bool IsMuted(Player* bot);
}  // namespace CoaBotMute

#endif
