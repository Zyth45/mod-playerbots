/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTBAGS_H
#define PLAYERBOTS_COAALTBAGS_H

#include <string>

class PlayerbotAI;

// "alt cmp|bags|sell|destroy" for the SquidBots addon (AiPlayerbot.CoaAltBags.Enable): compare an item with what
// the alt wears, and sell or destroy its bag items up to a quality after a preview (alt bags protocol).
namespace CoaAltBags
{
// True when the command is one of these four (it is then answered, even when the option is off).
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);

// The addon's request, put in the bot's queue: true when it is one of the four. Every request gets its reply,
// in order, on the bot's next update, in a fight or out of it (the engine's chat trigger kept one request of
// several asked at once, and the others never got their reply).
bool Enqueue(PlayerbotAI* botAI, std::string const& request);
void Drain(PlayerbotAI* botAI);
}

#endif
