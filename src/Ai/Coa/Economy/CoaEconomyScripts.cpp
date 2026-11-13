/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: every CoA Economy script is registered here, in this order.
// Called once by AddPlayerbotsScripts (src/Script/Playerbots.cpp); a new CoA Economy script goes here.

void AddSC_coa_economy_ahbot();  // AuctionHouse/CoaAuctionHouseBotHooks.cpp
void AddSC_coa_economy_market(); // Market/CoaMarketPrices.cpp
void AddSC_coa_economy_launch(); // Launch/CoaLaunchCurve.cpp
void AddSC_coa_economy_bot_auctions(); // Rpg/CoaBotAuctions.cpp
void AddSC_coa_economy_artisans(); // Craft/CoaArtisans.cpp
void AddSC_coa_economy_craft_orders(); // Craft/CoaCraftOrders.cpp
void AddSC_coa_economy_data(); // Data/CoaEconomyData.cpp
void AddSC_coa_economy_sellable(); // Market/CoaSellable.cpp
void AddSC_coa_economy_config(); // CoaEconomyConfig.cpp
void AddSC_coa_economy_ah_migration(); // AuctionHouse/CoaAhMigration.cpp
void AddSC_coa_economy_auction_clock(); // Market/CoaAuctionClock.cpp
void AddSC_coa_economy_materials(); // Craft/CoaMaterials.cpp
void AddSC_coa_economy_artisans_addon(); // Craft/CoaArtisansAddon.cpp
void AddSC_coa_economy_rare_recipes(); // Craft/CoaRareRecipes.cpp
void AddSC_coa_economy_bot_level_gold(); // Rpg/CoaBotLevelGold.cpp
void AddSC_coa_economy_chatter(); // Rpg/CoaEconomyChatter.cpp

void AddCoaEconomyScripts()
{
    AddSC_coa_economy_config();
    AddSC_coa_economy_ah_migration(); // before the AHBot: the old stock goes before the new one is posted
    AddSC_coa_economy_sellable();
    AddSC_coa_economy_ahbot();
    AddSC_coa_economy_market();
    AddSC_coa_economy_launch();
    AddSC_coa_economy_bot_auctions();
    AddSC_coa_economy_artisans();
    AddSC_coa_economy_craft_orders();
    AddSC_coa_economy_data();
    AddSC_coa_economy_auction_clock();
    AddSC_coa_economy_materials();
    AddSC_coa_economy_artisans_addon();
    AddSC_coa_economy_rare_recipes();
    AddSC_coa_economy_bot_level_gold();
    AddSC_coa_economy_chatter();
}
