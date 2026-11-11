/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaMaterials.h"

#include "CoaEconomyConf.h"
#include "CoaEconomyData.h"

#include "AuctionHouseMgr.h"
#include "CoaArtisans.h"
#include "CoaEconomyRules.h"
#include "CoaSellable.h"
#include "Config.h"
#include "Creature.h"
#include "Item.h"
#include "ItemUsageValue.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <vector>

namespace CoaEconomy::Materials
{

namespace
{
    struct Settings
    {
        bool enabled = false;
        bool smelt = true;
        bool prospect = true;
        bool disenchant = true;
        uint32 perVisit = 5;
        bool stock = false;
        uint32 maxStacks = 2;
        uint32 buyBelowPercent = 90;
        uint32 maxScan = 2000;
    };
    Settings settings;

    constexpr uint32 ProspectStack = 5;

    template <typename Fn>
    void ForEachBagItem(Player* bot, Fn&& fn)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        fn(item);
    }

    ItemUsage UsageOf(PlayerbotAI* botAI, Item* item)
    {
        return botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
    }

    bool HoldsAll(Player* bot, SpellInfo const* spell)
    {
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            if (spell->Reagent[i] > 0 && spell->ReagentCount[i] &&
                bot->GetItemCount(uint32(spell->Reagent[i])) < spell->ReagentCount[i])
                return false;
        return true;
    }

    // What a roll of loot is worth; 0 when it holds an item CoA does not have (nothing is broken then).
    uint64 LootValue(Loot const& loot)
    {
        uint64 value = 0;
        for (LootItem const& item : loot.items)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
            if (!proto || Sellable::IsLaterExpansion(proto))
                return 0;
            value += Artisans::MarketPrice(item.itemid) * item.count;
        }
        return value;
    }

    // Stores what came out; what does not fit is lost, as when a player's bags overflow (room is checked before).
    void StoreLoot(Player* bot, Loot const& loot)
    {
        for (LootItem const& item : loot.items)
        {
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item.itemid, item.count) == EQUIP_ERR_OK)
                bot->StoreNewItem(dest, item.itemid, true, item.randomPropertyId);
        }
    }

    // Smelting: ore into bars, with the reagents held only (nothing is bought).
    uint32 Smelt(Player* bot, uint32 budget)
    {
        uint32 done = 0;
        if (!Artisans::IsEnabled() || !bot->HasSkill(SKILL_MINING))
            return 0;
        for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
        {
            if (recipe.skillId != SKILL_MINING)
                continue;
            while (done < budget && HoldsAll(bot, recipe.spell) &&
                   CoaEconomyRules::WorthBreaking(recipe.materialCost, recipe.productValue))
            {
                if (!Artisans::Craft(bot, recipe))
                    return done;
                ++done;
            }
        }
        return done;
    }

    // Disenchanting: the greens, blues and epics it would sell, when the dust and essences are worth more.
    uint32 Disenchant(Player* bot, PlayerbotAI* botAI, uint32 budget)
    {
        uint32 const skill = bot->GetSkillValue(SKILL_ENCHANTING);
        if (!skill)
            return 0;
        std::vector<Item*> candidates;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || !proto->DisenchantID || proto->RequiredDisenchantSkill > skill || item->IsInTrade() ||
                proto->Quality < ITEM_QUALITY_UNCOMMON || proto->Quality > ITEM_QUALITY_EPIC)
                return;
            ItemUsage const usage = UsageOf(botAI, item);
            if (usage == ITEM_USAGE_AH || usage == ITEM_USAGE_VENDOR)
                candidates.push_back(item);
        });

        uint32 done = 0;
        for (Item* item : candidates)
        {
            if (done >= budget)
                break;
            ItemTemplate const* proto = item->GetTemplate();
            Loot loot;
            if (!loot.FillLoot(proto->DisenchantID, LootTemplates_Disenchant, bot, true, true) || loot.items.empty())
                continue;
            if (!CoaEconomyRules::WorthBreaking(Artisans::MarketPrice(proto->ItemId) * item->GetCount(), LootValue(loot)))
                continue;
            if (bot->GetFreeInventorySpace() + 1 < loot.items.size())
                break;
            bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            StoreLoot(bot, loot);
            for (LootItem const& made : loot.items)
                Data::RecordBreak(true, made.itemid, made.count, 1);
            ++done;
        }
        return done;
    }

    // Prospecting: five ores into gems, when the gems are worth more.
    uint32 Prospect(Player* bot, uint32 budget)
    {
        uint32 const skill = bot->GetSkillValue(SKILL_JEWELCRAFTING);
        if (!skill)
            return 0;
        std::unordered_set<uint32> ores;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (proto && (proto->Flags & ITEM_FLAG_IS_PROSPECTABLE) && proto->RequiredSkillRank <= skill && !item->IsInTrade())
                ores.insert(proto->ItemId);
        });

        uint32 done = 0;
        for (uint32 ore : ores)
            while (done < budget && bot->GetItemCount(ore) >= ProspectStack)
            {
                Loot loot;
                if (!loot.FillLoot(ore, LootTemplates_Prospecting, bot, true, true) || loot.items.empty())
                    break;
                if (!CoaEconomyRules::WorthBreaking(Artisans::MarketPrice(ore) * ProspectStack, LootValue(loot)) ||
                    bot->GetFreeInventorySpace() < loot.items.size())
                    break;
                bot->DestroyItemCount(ore, ProspectStack, true);
                StoreLoot(bot, loot);
                for (LootItem const& made : loot.items)
                    Data::RecordBreak(false, made.itemid, made.count, ProspectStack);
                ++done;
            }
        return done;
    }
}

void LoadSettings()
{
    settings.enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Materials.Enable", true, false);
    settings.smelt = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Materials.Smelt", true);
    settings.prospect = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Materials.Prospect", true);
    settings.disenchant = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Materials.Disenchant", true);
    settings.perVisit = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Materials.PerVisit", 5);
    settings.stock = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Stock.Enable", true, false);
    settings.maxStacks = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Artisans.Stock.MaxStacks", 2);
    settings.buyBelowPercent = std::min<uint32>(100,
        CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Artisans.Stock.BuyBelowPercent", 90));
}

bool IsEnabled()
{
    return settings.enabled;
}

bool IsStockEnabled()
{
    return settings.stock && Artisans::IsEnabled();
}

uint32 Process(Player* bot, PlayerbotAI* botAI)
{
    if (!settings.enabled || !bot || !botAI)
        return 0;
    uint32 done = 0;
    if (settings.smelt)
        done += Smelt(bot, settings.perVisit - done);
    if (settings.prospect && done < settings.perVisit)
        done += Prospect(bot, settings.perVisit - done);
    if (settings.disenchant && done < settings.perVisit)
        done += Disenchant(bot, botAI, settings.perVisit - done);
    return done;
}

std::unordered_set<uint32> StockReagents(Player* bot)
{
    std::unordered_set<uint32> reagents;
    if (!IsStockEnabled() || !bot)
        return reagents;
    for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            if (recipe.spell->Reagent[i] > 0 && recipe.spell->ReagentCount[i])
                reagents.insert(uint32(recipe.spell->Reagent[i]));
    return reagents;
}

bool KeepsInStock(Player* bot, Item* item, std::unordered_set<uint32> const& reagents)
{
    if (!bot || !item || !reagents.count(item->GetEntry()))
        return false;
    return CoaEconomyRules::KeepsInStock(bot->GetItemCount(item->GetEntry()), item->GetTemplate()->GetMaxStackSize(),
                                         settings.maxStacks);
}

bool BuyStock(Player* bot, ObjectGuid auctioneer, Creature* npc, uint64 budget)
{
    if (!IsStockEnabled() || !bot || !npc || !budget)
        return false;
    std::unordered_set<uint32> const reagents = StockReagents(bot);
    if (reagents.empty())
        return false;
    AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(npc->GetFaction());
    if (!house)
        return false;

    // The cheapest lot per item, well under its market price, of a reagent it still has room to stock.
    AuctionEntry const* best = nullptr;
    uint64 bestPerItem = 0;
    uint32 scanned = 0;
    for (auto const& entry : house->GetAuctions())
    {
        if (++scanned > settings.maxScan)
            break;
        AuctionEntry const* auction = entry.second;
        if (!auction || !auction->buyout || auction->buyout > budget || auction->owner == bot->GetGUID() ||
            auction->bidder == bot->GetGUID() || !reagents.count(auction->item_template))
            continue;
        Item* item = sAuctionMgr->GetAItem(auction->item_guid);
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
        if (!item || !proto || !item->GetCount())
            continue;
        if (!CoaEconomyRules::KeepsInStock(bot->GetItemCount(proto->ItemId) + item->GetCount(), proto->GetMaxStackSize(),
                                           settings.maxStacks))
            continue;
        uint64 const perItem = auction->buyout / item->GetCount();
        if (!CoaEconomyRules::StockBargain(perItem, Artisans::MarketPrice(proto->ItemId), settings.buyBelowPercent))
            continue;
        if (!best || perItem < bestPerItem)
        {
            best = auction;
            bestPerItem = perItem;
        }
    }
    if (!best)
        return false;

    WorldPacket packet(CMSG_AUCTION_PLACE_BID);
    packet << auctioneer << uint32(best->Id) << uint32(best->buyout);
    bot->GetSession()->HandleAuctionPlaceBid(packet);
    return true;
}

} // namespace CoaEconomy::Materials

namespace
{
    class CoaMaterialsWorld : public WorldScript
    {
    public:
        CoaMaterialsWorld() : WorldScript("CoaMaterialsWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { CoaEconomy::Materials::LoadSettings(); }
    };
}

void AddSC_coa_economy_materials()
{
    // Off by default; the two Enable keys are read at start-up (restart to change them).
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Materials.Enable", true, false) &&
        !CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Stock.Enable", true, false))
        return;
    CoaEconomy::Materials::LoadSettings();
    new CoaMaterialsWorld();
}
