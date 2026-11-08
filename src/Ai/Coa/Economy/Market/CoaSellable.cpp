/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSellable.h"

#include "CoaEconomyRules.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ScriptMgr.h"

#include <unordered_set>

namespace CoaEconomy
{
char const* OriginalWorldSourcesQuery(); // AuctionHouse/CoaAuctionHouseBot.cpp
}

namespace CoaEconomy::Sellable
{

namespace
{
    // Filled once on the world thread at start-up, before any bot update; read-only afterwards.
    std::unordered_set<uint32> vendorItems;
    std::unordered_set<uint32> originalWorldItems;

    void Load()
    {
        if (QueryResult result = WorldDatabase.Query("SELECT DISTINCT item FROM npc_vendor WHERE item > 0"))
            do
            {
                vendorItems.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        if (QueryResult result = WorldDatabase.Query(OriginalWorldSourcesQuery()))
            do
            {
                originalWorldItems.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        LOG_INFO("module", "CoA Economy: {} vendor goods and {} original-world items for the bots' trade",
                 vendorItems.size(), originalWorldItems.size());
    }

    class CoaSellableWorld : public WorldScript
    {
    public:
        CoaSellableWorld() : WorldScript("CoaSellableWorld", {WORLDHOOK_ON_STARTUP}) { }

        void OnStartup() override { Load(); }
    };
}

bool IsVendorItem(uint32 itemId)
{
    return vendorItems.count(itemId) != 0;
}

bool IsLaterExpansion(ItemTemplate const* proto)
{
    return proto && CoaEconomyRules::IsLaterExpansion(proto->ItemId, proto->RequiredLevel, proto->ItemLevel,
                                                      originalWorldItems.count(proto->ItemId) != 0);
}

bool BotMayList(ItemTemplate const* proto, bool crafted)
{
    return proto && CoaEconomyRules::BotMayList(proto->Class, crafted, IsVendorItem(proto->ItemId),
                                                IsLaterExpansion(proto));
}

} // namespace CoaEconomy::Sellable

void AddSC_coa_economy_sellable()
{
    // Needed only by the bots' trade; both are off by default and read at start-up.
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Enable", false, false) &&
        !sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", false, false))
        return;
    new CoaEconomy::Sellable::CoaSellableWorld();
}
