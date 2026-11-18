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
// his own bots, alts or recruited. "mute on|off" (or "mute" alone: the state) whispered on SQBA by the bot's master,
// answered "MUTE\t<0|1>". A muted bot says no CoA Live line, whispers nothing unasked and says no random line; it
// still answers its master's orders and questions. The mute lasts while the bot keeps that master (not saved).
namespace CoaBotMute
{
// The command of the queue (CoaAltBags::Command): true when it was "mute".
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);
// The bot was muted by its present master.
bool IsMuted(Player* bot);
}  // namespace CoaBotMute

#endif
