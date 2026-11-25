/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAECONOMYEVENTS_H
#define PLAYERBOTS_COAECONOMYEVENTS_H

#include "Define.h"

class Item;
class Player;
struct ItemTemplate;

// CoA 3.3.1 (AiPlayerbot.CoaEconomy.Events.*, on by default): the economy's facts the dashboard follows, written by
// the server in the characters database (tables made at start-up, rows queued and written once a minute, off the
// world thread). Form in D:\CoA-Commun\tmp\coa_formula_event-schema.md.
//  - coa_formula_event: a formula a character learned beyond the trainers (from its bags, drawn, bought).
//  - coa_enchant_order: an enchantment an enchanter bot put on for another bot, with the price and the fee it earned.
// Safe from any thread.
namespace CoaEconomy::Events
{
// how: "bags", "need", "drawn", "auction" or "vendor" (anything else is dropped); recipe may be null (a drawn formula
// still names the formula item it came from); price in copper, 0 when none was paid.
void NoteFormula(Player* learner, uint32 skill, uint32 spell, ItemTemplate const* recipe, char const* how, uint64 price);
void NoteOrder(Player* enchanter, Player* customer, Item* item, uint32 enchantId, uint64 price, uint64 fee);
}

void AddSC_coa_economy_events();

#endif
