/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: a time budget for the bots' AI per map tick (AiPlayerbot.CoaWorld.AiBudgetMs, CoaWorldAiBudget.cpp).

#ifndef PLAYERBOTS_COAWORLDAIBUDGET_H
#define PLAYERBOTS_COAWORLDAIBUDGET_H

#include "CoaWorldAiBudgetRules.h"
#include "Define.h"

class Player;
class PlayerbotAI;


namespace CoaWorldAiBudget
{
bool On();
// Map thread, before the bot's AI: true when it skips its AI this tick.
bool Skip(Player* bot, PlayerbotAI* botAI);
// Map thread, after the bot's AI: the time it took (microseconds), counted against its map's tick.
void Spent(Player* bot, uint64 us);
}  // namespace CoaWorldAiBudget

void AddSC_coa_world_ai_budget();

#endif
