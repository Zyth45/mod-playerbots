/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSellable.h"

#include "CoaEconomyConf.h"

#include "CoaEconomyRules.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <unordered_map>
#include <vector>

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
    std::unordered_set<uint32> freeVendorItems; // sold without a stock limit by a vendor that stands in the world
    std::unordered_set<uint32> originalWorldItems;
    std::unordered_set<uint32> laterCrafted;    // only made from later expansion reagents (Fel Iron Bar kits)

    // The item a trade skill spell makes, or 0.
    uint32 ProductOf(SpellInfo const* spell)
    {
        if (!spell || !spell->HasAttribute(SPELL_ATTR0_IS_TRADESKILL))
            return 0;
        for (SpellEffectInfo const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
                return effect.ItemType;
        return 0;
    }

    std::vector<bool> ReagentsLater(SpellInfo const* spell, std::function<bool(uint32)> const& isLater)
    {
        std::vector<bool> out;
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            if (spell->Reagent[i] > 0 && spell->ReagentCount[i])
                out.push_back(isLater(uint32(spell->Reagent[i])));
        return out;
    }

    void Load()
    {
        if (QueryResult result = WorldDatabase.Query("SELECT DISTINCT item FROM npc_vendor WHERE item > 0"))
            do
            {
                vendorItems.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        // A crafted product is a vendor good only when anyone can buy it at will: unlimited stock, a spawned vendor (a
        // potion a quartermaster sells 3 at a time stays an artisan's product).
        if (QueryResult result = WorldDatabase.Query("SELECT DISTINCT nv.item FROM npc_vendor nv WHERE nv.item > 0 AND "
                                                     "nv.maxcount = 0 AND EXISTS (SELECT 1 FROM creature c WHERE c.id = nv.entry)"))
            do
            {
                freeVendorItems.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        if (QueryResult result = WorldDatabase.Query(OriginalWorldSourcesQuery()))
            do
            {
                originalWorldItems.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        laterCrafted = LaterCraftedItems([](uint32 itemId)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            return proto && CoaEconomyRules::IsLaterExpansion(proto->ItemId, proto->RequiredLevel, proto->ItemLevel,
                                                              originalWorldItems.count(proto->ItemId) != 0);
        });

        LOG_INFO("module", "CoA Economy: {} vendor goods, {} original-world items and {} later expansion crafts for "
                 "the bots' trade", vendorItems.size(), originalWorldItems.size(), laterCrafted.size());
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

bool IsFreeVendorItem(uint32 itemId)
{
    return freeVendorItems.count(itemId) != 0;
}

bool IsLaterExpansion(ItemTemplate const* proto)
{
    return proto && (CoaEconomyRules::IsLaterExpansion(proto->ItemId, proto->RequiredLevel, proto->ItemLevel,
                                                       originalWorldItems.count(proto->ItemId) != 0) ||
                     laterCrafted.count(proto->ItemId) != 0);
}

std::unordered_set<uint32> LaterCraftedItems(std::function<bool(uint32)> const& isLater)
{
    std::unordered_map<uint32, std::vector<bool>> recipesLater; // product -> one flag per recipe
    for (uint32 id = 0; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(id);
        uint32 const product = ProductOf(spell);
        if (product)
            recipesLater[product].push_back(CoaEconomyRules::RecipeIsLaterExpansion(false, ReagentsLater(spell, isLater)));
    }
    std::unordered_set<uint32> out;
    for (auto const& [product, flags] : recipesLater)
        if (CoaEconomyRules::OnlyFromLaterRecipes(flags))
            out.insert(product);
    return out;
}

bool HasLaterReagent(SpellInfo const* spell)
{
    return spell && CoaEconomyRules::RecipeIsLaterExpansion(false, ReagentsLater(spell, [](uint32 itemId)
    {
        return IsLaterExpansion(sObjectMgr->GetItemTemplate(itemId));
    }));
}

bool BotMayList(ItemTemplate const* proto, bool crafted)
{
    bool const vendorGood = crafted ? freeVendorItems.count(proto ? proto->ItemId : 0) != 0 : IsVendorItem(proto ? proto->ItemId : 0);
    return proto && CoaEconomyRules::BotMayList(proto->Class, crafted, vendorGood,
                                                IsLaterExpansion(proto));
}

} // namespace CoaEconomy::Sellable

void AddSC_coa_economy_sellable()
{
    // Needed only by the bots' trade; both are off by default and read at start-up.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Enable", true, false) &&
        !CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", true, false))
        return;
    new CoaEconomy::Sellable::CoaSellableWorld();
}
