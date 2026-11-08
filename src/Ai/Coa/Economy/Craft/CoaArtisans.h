/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_ARTISANS_H
#define COA_ECONOMY_ARTISANS_H

#include "Define.h"

#include <string>
#include <vector>

class Item;
class Player;
class SpellInfo;

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

    // The profession's name in the bot's language (English when it has none).
    std::string ProfessionName(Player* bot, uint32 skillId);

    // One item's market price (learned price, else the house bot's, else a vendor-based guess), copper.
    uint64 MarketPrice(uint32 itemId);
}

void AddSC_coa_economy_artisans();

#endif
