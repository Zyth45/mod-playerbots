/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASOUL_H
#define PLAYERBOTS_COASOUL_H

#include "Common.h"
#include "CoaSoulRules.h"

class Player;

// The souls of the random bots (AiPlayerbot.CoaSoul.Enable, CoaSoulRules.h, contract ACCROCHES-LLM-3.0 §1).
namespace CoaSoul
{
bool Enabled();
// The soul of a random bot: from the table when it has one there, else drawn from its guid (and written later).
// Any game thread.
Soul Get(Player* bot);
// The weight of a "new rpg" status for this bot: the configured one, tilted by its soul (unchanged when off or not
// a random bot).
uint32 RpgWeight(Player* bot, uint32 status, int32 configured);
// At each config load: the weights, and the souls of the table (playerbots_coa_soul).
void Load();
// World thread, now and then: the new souls into the table, in one asynchronous statement.
void Flush(uint64 nowMs);
}  // namespace CoaSoul

#endif
