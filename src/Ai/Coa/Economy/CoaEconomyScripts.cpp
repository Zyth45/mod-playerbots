/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: every script of the session is registered here, in this order.
// Called once by AddPlayerbotsScripts (src/Script/Playerbots.cpp); a new script of the session goes here.

void AddSC_coa_economy_ahbot();  // AuctionHouse/CoaAuctionHouseBotHooks.cpp

void AddCoaEconomyScripts()
{
    AddSC_coa_economy_ahbot();
}
