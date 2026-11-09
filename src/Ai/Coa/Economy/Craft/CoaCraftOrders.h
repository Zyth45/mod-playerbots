/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_CRAFT_ORDERS_H
#define COA_ECONOMY_CRAFT_ORDERS_H

#include "CoaArtisans.h"

class Item;
class Player;

// CoA Economy (lot 4): artisan bots talk business in Trade and take orders by whisper, through CoA Live's bank
// engine (lines from CoA IA MIND, bank 3_0_05) and CoA Live's COD mail (AiPlayerbot.CoaEconomy.CraftOrders.*, off by
// default; does nothing either while AiPlayerbot.CoaLive.Enable is 0).
// - "can you make [item]?" whispered to a bot that knows the recipe: it quotes a price (materials at market price
//   plus its fee), crafts the piece and mails it cash on delivery (wh_craft_quote, wh_craft_cod); otherwise it says
//   it cannot (wh_craft_cannot) or that it is busy with another order (wh_craft_busy).
// - In a capital, an artisan back from the auction house may offer the piece it just made in Trade (trade_craft,
//   an offer a player can answer: COD at the price said) or say it takes orders (trade_craft_lfw).
// World thread only.
namespace CoaEconomy::CraftOrders
{
    bool IsEnabled();

    // An artisan just crafted this piece (auction house visit): offer it in Trade instead of listing it.
    // True when the offer was said (the item then stays in the bags, held for the buyer).
    bool OfferInTrade(Player* bot, Item* crafted, unsigned long long price);

    // The artisan's price for one craft of this recipe (materials, fee, its patience), copper.
    uint32 QuoteFor(Player* bot, Artisans::Recipe const& recipe);
    // Within OrderCooldownMinutes of its last order; MarkBusy starts that cooldown.
    bool IsBusy(Player* bot);
    void MarkBusy(Player* bot);
}

void AddSC_coa_economy_craft_orders();

#endif
