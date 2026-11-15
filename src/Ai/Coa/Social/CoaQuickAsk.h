/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAQUICKASK_H
#define _PLAYERBOT_COAQUICKASK_H

#include <cstdint>
#include <string>

#include "CoaQuickAskRules.h"

class Player;
class PlayerbotAI;
class SpellInfo;
class Unit;

// "Heal please", "CC please", "Taunt please" of a real player to the bots of its group (CoaQuickAskRules.h). Heard
// by the chat hooks (any thread) or the addon (the bot's queue), done by the bots of the group in their own update.
namespace CoaQuickAsk
{
// A line of a real player of a group: true when it was an ask (heard, or refused). receiver: the bot whispered to.
bool HearChat(Player* player, std::string const& msg, Origin origin, Player* receiver);

// "qa <heal|cc|taunt> [<guid hex>]" and "qastate" of the addon, run from the bot's queue. True when it was one.
bool Command(PlayerbotAI* botAI, std::string const& command, std::string const& arg);

// A bot's update: the open asks of its group's players.
void Tick(Player* bot, PlayerbotAI* botAI);

// The bot's best crowd control (CcSpell order), valid on `target` when given; readyOnly: off cooldown. 0: none.
uint32_t BestCc(Player* bot, Unit* target, bool readyOnly);

// The key spells bar's category of a spell of the bot ("def", "burst", "cc", "-"); botCc: BestCc(bot, nullptr, false).
char const* SpellCategory(SpellInfo const* info, uint32_t botCc);
}  // namespace CoaQuickAsk

#endif
