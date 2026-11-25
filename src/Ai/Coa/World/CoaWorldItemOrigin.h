/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAWORLDITEMORIGIN_H
#define PLAYERBOTS_COAWORLDITEMORIGIN_H

#include "Define.h"
#include "ObjectGuid.h"

class Item;
class Player;
enum EnchantmentSlot : uint8;

namespace CoaWorld
{
// How an item came: written to characters.coa_item_origin when it is worn (CoaWorldItemOrigin.cpp). source is a
// string literal ("dungeonsim", ...), detail its entry (a creature, a quest, a vendor, a map) or 0.
void NoteItemOrigin(Item* item, char const* source, uint32 detail);

// 3.3.1: a piece made by a crafter with no profession spell (a bot's craft: StoreNewItem, Item::CreateItem for a
// mail order). Worn by its maker it is "selfcraft", by anyone else "craft"; detail is the recipe's spell (or 0).
void NoteCraft(Item* item, Player* maker, uint32 spell);
void NoteCraft(Item* item, ObjectGuid maker, uint32 spell);  // a maker offline (a mail order)

// 3.3.1: puts an enchantment on an item of owner (off, set, on: what the bots' code did by hand) and writes an
// "enchant" event to characters.coa_item_event when it changed. source: "bot" (a bot's command), "artisan" (a bot
// enchanter, Economy), detail the enchantment (SpellItemEnchantment.dbc); "autogear" (the factory): no "enchant"
// row, the piece counts in the factory pass's single "regear" row.
void SetEnchant(Player* owner, Item* item, EnchantmentSlot slot, uint32 enchantId, ObjectGuid caster,
                char const* source);

// While one lives, the pieces this thread equips with no origin noted are "start" gear: the factory's and the
// auto-gear's only. Outside it, a piece of unseen origin gets no row.
class StartGearScope
{
public:
    StartGearScope();
    ~StartGearScope();
    StartGearScope(StartGearScope const&) = delete;
    StartGearScope& operator=(StartGearScope const&) = delete;
};
}

#endif
