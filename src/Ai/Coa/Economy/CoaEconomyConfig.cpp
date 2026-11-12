/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: its settings kept in PlayerbotAIConfig. Called by PlayerbotAIConfig::Initialize();
// a new CoA Economy setting goes here (its member in the "CoA Economy" block of PlayerbotAIConfig.h).
// The auction house bot reads its own AiPlayerbot.CoaEconomy.* keys (AuctionHouse/, Market/, Launch/).

#include "Config.h"

#include "CoaEconomyConf.h"
#include "PlayerbotAIConfig.h"
#include "ScriptMgr.h"

void PlayerbotAIConfig::LoadCoaEconomyConfig()
{
    coaEconomyRpgSellEnable = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.RpgSell.Enable", true);
    coaEconomyRpgSellBagPercent =
        std::min<uint32>(100, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.RpgSell.BagPercent", 70));
}

namespace
{
    // ".reload config" reloads the files but not PlayerbotAIConfig: the CoA Economy keys are read again here.
    class CoaEconomyConfigWorld : public WorldScript
    {
    public:
        CoaEconomyConfigWorld() : WorldScript("CoaEconomyConfigWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }

        void OnAfterConfigLoad(bool reload) override
        {
            if (reload)
                sPlayerbotAIConfig.LoadCoaEconomyConfig();
        }
    };
}

void AddSC_coa_economy_config()
{
    new CoaEconomyConfigWorld();
}
