/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: every script of the session is registered here, in this order.
// Called once by AddPlayerbotsScripts (src/Script/Playerbots.cpp); a new script of the session goes here.

void AddSC_coa_economy_ahbot();  // AuctionHouse/CoaAuctionHouseBotHooks.cpp
void AddSC_coa_economy_market(); // Market/CoaMarketPrices.cpp
void AddSC_coa_economy_launch(); // Launch/CoaLaunchCurve.cpp
void AddSC_coa_economy_bot_auctions(); // Rpg/CoaBotAuctions.cpp
void AddSC_coa_economy_artisans(); // Craft/CoaArtisans.cpp
void AddSC_coa_economy_craft_orders(); // Craft/CoaCraftOrders.cpp
void AddSC_coa_economy_data(); // Data/CoaEconomyData.cpp

void AddCoaEconomyScripts()
{
    AddSC_coa_economy_ahbot();
    AddSC_coa_economy_market();
    AddSC_coa_economy_launch();
    AddSC_coa_economy_bot_auctions();
    AddSC_coa_economy_artisans();
    AddSC_coa_economy_craft_orders();
    AddSC_coa_economy_data();
}
