/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAKEYSPELLS_H
#define PLAYERBOTS_COAKEYSPELLS_H

#include <cstdint>
#include <string>

class PlayerbotAI;

// The addon's key spells bar (AiPlayerbot.CoaKeySpells.Enable, CoaKeySpellsRules.h), for any bot of the master's
// group: "keyspells" and "keepcd <spellId> on|off", answered from the bot's queue (CoaAltBags::Enqueue/Drain).
namespace CoaKeySpells
{
// True when the command is one of these two (it is then answered, even when the option is off).
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);

// True when the AI must not cast this spell by itself: the player keeps its cooldown (any rank).
bool Kept(PlayerbotAI* botAI, uint32_t spellId);
}

#endif
