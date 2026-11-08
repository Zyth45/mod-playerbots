/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_SELLABLE_H
#define COA_ECONOMY_SELLABLE_H

#include "Define.h"

struct ItemTemplate;

// What the bots may trade on CoA: the vendor goods and the original-world item sources, read once at start-up
// when BotAuctions or Artisans is on. The rules themselves are in CoaEconomyRules.h.
namespace CoaEconomy::Sellable
{

bool IsVendorItem(uint32 itemId);
// No source in the original game's world (Netherweave, Fel Iron...). Before the load, every candidate is out.
bool IsLaterExpansion(ItemTemplate const* proto);
// What a bot may list at the auction house (crafted: made by its artisan during this visit).
bool BotMayList(ItemTemplate const* proto, bool crafted);

} // namespace CoaEconomy::Sellable

void AddSC_coa_economy_sellable();

#endif
