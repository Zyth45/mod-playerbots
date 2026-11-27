/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_MATERIALS_H
#define COA_ECONOMY_MATERIALS_H

// The guild stock API below (IsGuildStockEnabled, IsStockItem, StockCount, StockItems) is here.
#define COA_ECONOMY_GUILD_STOCK 1

#include "Define.h"
#include "ObjectGuid.h"

#include <unordered_set>
#include <vector>

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
    // 3.2.2 (EnchantForSelf.Reagents): at a vendor, before the sale, an enchanter disenchants up to Materials.PerVisit
    // of the green or better pieces it would sell (Disenchant's rule); returns how many.
    uint32 DisenchantBeforeSale(Player* bot, PlayerbotAI* botAI);

    // The reagents an artisan keeps (empty when the stock is off or it has no recipe).
    std::unordered_set<uint32> StockReagents(Player* bot);
    // Kept from the sale: one of those reagents, while the bot holds no more than Stock.MaxStacks stacks of it.
    bool KeepsInStock(Player* bot, Item* item, std::unordered_set<uint32> const& reagents);

    // Buys one lot of a stock reagent below Stock.BuyBelowPercent of its market price, within budget (buyout). With
    // the guild stock: only within the guild's daily share of the house (CoaEconomyRules::GuildMayBuy).
    bool BuyStock(Player* bot, ObjectGuid auctioneer, Creature* npc, uint64 budget);

    // Guild stock (CoaEconomy.GuildStock.*, on by default): gatherers keep a few kinds of what they gather (cloth
    // for all, ore and bars, herbs, leather), artisans a stack of the intermediates they make (bars, bolts, cured
    // leather), in their bags and their bank, for a guildmate's order.
    bool IsGuildStockEnabled();
    // GuildStock.Stacks: full stacks of each material the stock keeps (1 to 5).
    uint32 GuildStockStacks();
    // At an auction house visit: makes intermediates from the materials held (up to GuildStock.CraftsPerVisit),
    // then puts the full stacks of stock beyond the first in the bank. Returns the items moved or made.
    uint32 TendGuildStock(Player* bot);
    // 3.1.4: the bot has guild stock to put in its bank or surplus to sell: a reason to visit an auctioneer (RpgTrade).
    // Looked at once a minute per bot at most.
    bool HasStockWork(Player* bot);

    // For CoA World (guild orders): an item the bot keeps in stock, and how many it holds, bags and bank.
    bool IsStockItem(Player* bot, uint32 entry);
    uint32 StockCount(Player* bot, uint32 entry);
    // The stacks of that item, bags first, then the bank (to mail some to a guildmate: split what is needed).
    std::vector<Item*> StockItems(Player* bot, uint32 entry);
}

void AddSC_coa_economy_materials();

#endif
