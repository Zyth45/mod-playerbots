/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_ARTISANS_H
#define COA_ECONOMY_ARTISANS_H

#include "CoaRealPrices.h"
#include "Define.h"

#include <string>
#include <vector>

class Item;
class Player;
class SpellInfo;
struct ItemTemplate;

// CoA Economy (lot 4): bots with a profession craft from the recipes they really know (AiPlayerbot.CoaEconomy.
// Artisans.*, off by default). A recipe is any spell the bot knows, of a primary or secondary profession skill
// line (from the client data, so CoA's own professions count), that creates a tradable item. Its cost is that of
// its reagents at market price; reagents the bot holds are used, the missing ones are paid in gold at that price
// (the bot "buys" them), so gold leaves the economy as if it had shopped. World thread only.
namespace CoaEconomy::Artisans
{
    bool IsEnabled();
    void LoadSettings();

    struct Recipe
    {
        SpellInfo const* spell = nullptr;
        uint32 itemId = 0;
        uint32 count = 1;
        uint32 skillId = 0;      // the profession (SkillLine.dbc)
        uint64 materialCost = 0; // all reagents at market price, copper
        uint64 productValue = 0; // the product at market price, whole count, copper
    };

    // The bot's tradable recipes (empty when off or when it has no profession).
    std::vector<Recipe> KnownRecipes(Player* bot);

    // A recipe of the bot whose product sells for at least MinProfitPercent over its materials; the best of a few,
    // drawn so that artisans do not all make the same thing. False when there is none.
    bool PickProfitable(Player* bot, Recipe& out);

    // Crafts it: uses the reagents held, pays the missing ones, stores the product. Null when the bags are full or
    // the gold is short (nothing is taken then).
    Item* Craft(Player* bot, Recipe const& recipe);

    // What the reagents missing for `times` crafts cost at market price (the held ones are free), copper.
    uint64 MissingCost(Player* bot, Recipe const& recipe, uint32 times);
    // Uses the reagents of `times` crafts (held ones first, the rest paid in gold); the products are the caller's to
    // make. False, with nothing taken, when the gold is short.
    bool Consume(Player* bot, Recipe const& recipe, uint32 times);

    // The profession's name in the bot's language (English when it has none).
    std::string ProfessionName(Player* bot, uint32 skillId);

    // Clickable links for the chat: the profession (the bot's highest rank of it, a spell link; its name as text if it
    // has none) and a recipe (the trade skill link the client makes when one shift-clicks a recipe).
    std::string ProfessionLink(Player* bot, uint32 skillId);
    std::string RecipeLink(Player* bot, Recipe const& recipe);

    // One item's market price (learned price, else the house bot's, else a vendor-based guess), copper. With
    // BotAuctions.RealHousePrices: its FairPrice at the bots' factors.
    uint64 MarketPrice(uint32 itemId);

    // A price that looks human: the real CoA house's (scaled), a crafted good kept between 1.1 and 2 times its
    // reagents at the same prices, never under a vendor's buying price plus a quarter, never over what a vendor sells
    // it for at will. 0 when nothing is known.
    uint64 FairPrice(ItemTemplate const* proto, RealPrices::Multipliers const& multipliers);

    // What a reagent costs an artisan: its market price, or less when making it from its own reagents is cheaper
    // (Artisans.RecursiveCost; the market price alone when it is off).
    uint64 ReagentCost(uint32 itemId);
}

void AddSC_coa_economy_artisans();

#endif
