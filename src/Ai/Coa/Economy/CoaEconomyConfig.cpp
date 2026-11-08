/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: the session's settings kept in PlayerbotAIConfig. Called by PlayerbotAIConfig::Initialize();
// a new setting of the session goes here (its member in the "CoA Economy" block of PlayerbotAIConfig.h).
// The auction house bot reads its own AiPlayerbot.CoaEconomy.* keys (AuctionHouse/, Market/, Launch/).

#include "Config.h"
#include "PlayerbotAIConfig.h"

void PlayerbotAIConfig::LoadCoaEconomyConfig()
{
    coaEconomyRpgSellEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.RpgSell.Enable", false);
    coaEconomyRpgSellBagPercent =
        std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.RpgSell.BagPercent", 70));
}
