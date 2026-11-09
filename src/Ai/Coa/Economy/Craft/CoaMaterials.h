/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_MATERIALS_H
#define COA_ECONOMY_MATERIALS_H

#include "Define.h"
#include "ObjectGuid.h"

#include <unordered_set>

class Creature;
class Item;
class Player;
class PlayerbotAI;

// CoA Economy: the materials of the artisans (vision 3.0, section 2), at an auction house visit, world thread only.
//   Materials.*      smelting (Mining), prospecting (Jewelcrafting) and disenchanting (Enchanting) of what the bot
//                    would sell anyway, when what comes out is worth more at market price.
//   Artisans.Stock.* an artisan keeps the reagents of its recipes (not sold) and buys them cheap at the auction house.
// Both off by default.
namespace CoaEconomy::Materials
{
    void LoadSettings();
    bool IsEnabled();
    bool IsStockEnabled();

    // Smelts, prospects and disenchants up to Materials.PerVisit times; returns how many.
    uint32 Process(Player* bot, PlayerbotAI* botAI);

    // The reagents an artisan keeps (empty when the stock is off or it has no recipe).
    std::unordered_set<uint32> StockReagents(Player* bot);
    // Kept from the sale: one of those reagents, while the bot holds no more than Stock.MaxStacks stacks of it.
    bool KeepsInStock(Player* bot, Item* item, std::unordered_set<uint32> const& reagents);

    // Buys one lot of a stock reagent below Stock.BuyBelowPercent of its market price, within budget (buyout).
    bool BuyStock(Player* bot, ObjectGuid auctioneer, Creature* npc, uint64 budget);
}

void AddSC_coa_economy_materials();

#endif
