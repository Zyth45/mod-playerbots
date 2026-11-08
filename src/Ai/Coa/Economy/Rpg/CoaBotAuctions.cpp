/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotAuctions.h"

#include "AuctionHouseMgr.h"
#include "CoaAuctionHouseBot.h"
#include "CoaGearScore.h"
#include "CoaMarketPrices.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemUsageValue.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace CoaEconomy::BotAuctions
{

namespace
{
    struct Settings
    {
        bool enabled = false;
        uint32 cooldownSeconds = 45 * MINUTE;
        uint32 listPerVisit = 3;
        uint32 maxBudgetPercent = 30;
        uint32 visitsPerTick = 2;
        uint32 durationMinutes = 24 * 60;
        uint32 maxScan = 300;
    };
    Settings settings;

    struct Visit
    {
        ObjectGuid bot;
        ObjectGuid auctioneer;
    };

    // Filled by the map threads, emptied by the world thread.
    std::mutex queueLock;
    std::deque<Visit> queue;
    std::unordered_map<ObjectGuid::LowType, uint32> lastVisit; // under queueLock
    constexpr std::size_t MaxQueue = 200;

    // A stable number in [0, 1) per character and per trait.
    float Hash01(ObjectGuid guid, uint32 salt)
    {
        uint64 x = guid.GetCounter() * 0x9E3779B97F4A7C15ull + salt * 0xBF58476D1CE4E5B9ull;
        x ^= x >> 31;
        x *= 0x94D049BB133111EBull;
        x ^= x >> 29;
        return float(x % 10000) / 10000.0f;
    }

    void LoadSettings()
    {
        settings.enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Enable", false, false);
        settings.cooldownSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.VisitCooldownMinutes", 45) * MINUTE;
        settings.listPerVisit = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.ListPerVisit", 3);
        settings.maxBudgetPercent = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.MaxBudgetPercent", 30));
        settings.visitsPerTick = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.VisitsPerTick", 2));
        uint32 const hours = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.DurationHours", 24);
        settings.durationMinutes = (hours <= 12 ? 12 : hours <= 24 ? 24 : 48) * 60;
        settings.maxScan = std::max<uint32>(10, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.MaxScan", 300));
    }

    // The price a bot asks for one item: the market's learned price, else the house's, else a vendor-based guess.
    uint64 ReferencePrice(ItemTemplate const* proto)
    {
        if (MarketBand const* band = sCoaMarketPrices->GetBand(proto->ItemId))
            if (band->Usual)
                return band->Usual;
        if (uint64 house = auctionbot->GetReferencePricePerItem(proto))
            return house;
        return std::max<uint64>(100, uint64(proto->SellPrice) * 4);
    }

    // Gold of its sales and items it won: the auction mail a bot never opens otherwise.
    void CollectAuctionMail(Player* bot)
    {
        uint32 const now = uint32(GameTime::GetGameTime().count());
        bool changed = false;
        for (Mail* mail : bot->GetMails())
        {
            if (!mail || mail->state == MAIL_STATE_DELETED || mail->messageType != MAIL_AUCTION || mail->deliver_time > now)
                continue;

            if (mail->money && bot->ModifyMoney(int32(mail->money), false))
            {
                mail->money = 0;
                mail->state = MAIL_STATE_CHANGED;
                changed = true;
            }

            std::vector<ObjectGuid::LowType> taken;
            for (MailItemInfo const& info : mail->items)
            {
                Item* item = bot->GetMItem(info.item_guid);
                if (!item)
                    continue;
                ItemPosCountVec dest;
                if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
                    break; // bags full: the rest waits for the next visit
                taken.push_back(info.item_guid);
                bot->RemoveMItem(info.item_guid);
                bot->MoveItemToInventory(dest, item, true);
            }
            for (ObjectGuid::LowType guid : taken)
            {
                mail->RemoveItem(guid);
                mail->removedItems.push_back(guid);
            }
            if (!taken.empty())
            {
                mail->state = MAIL_STATE_CHANGED;
                changed = true;
            }
        }

        if (!changed)
            return;
        // Mail, items and gold saved together, so an item is never both in the mail and in the bags in the database.
        bot->m_mailsUpdated = true;
        bot->SaveToDB(false, false);
    }

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

    // Lists up to ListPerVisit items the bot's item usage marks for the auction house, through the core's packet.
    uint32 ListLoot(Player* bot, PlayerbotAI* botAI, ObjectGuid auctioneer, Traits const& traits)
    {
        std::vector<Item*> loot;
        ForEachBagItem(bot, [&](Item* item)
        {
            if (loot.size() >= settings.listPerVisit || item->IsSoulBound() || item->IsInTrade())
                return;
            if (botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get() == ITEM_USAGE_AH)
                loot.push_back(item);
        });

        // An impatient bot undercuts more: 85 % to 105 % of the reference price.
        double const factor = 0.85 + 0.20 * traits.patience;
        for (Item* item : loot)
        {
            uint64 const buyout = std::min<uint64>(MAX_MONEY_AMOUNT,
                std::max<uint64>(1, uint64(ReferencePrice(item->GetTemplate()) * factor) * item->GetCount()));
            uint64 const bid = std::max<uint64>(1, buyout * 3 / 4);

            WorldPacket packet(CMSG_AUCTION_SELL_ITEM);
            packet << auctioneer << uint32(1) << item->GetGUID() << uint32(item->GetCount())
                   << uint32(bid) << uint32(buyout) << uint32(settings.durationMinutes);
            bot->GetSession()->HandleAuctionSellItem(packet);
        }
        return uint32(loot.size());
    }

    bool HasFreeBagSlot(Player* bot)
    {
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot))
                return true;
        return false;
    }

    // One purchase per visit: the best upgrade it can afford (CoA Combat's gear score), else a bag for an empty bag
    // slot. Buyout only, so the item comes at once by mail; it is collected at the next visit.
    bool BuyOne(Player* bot, ObjectGuid auctioneer, Creature* npc, Traits const& traits)
    {
        // A thrifty bot spends down to 30 % of its share.
        uint64 const budget = uint64(double(bot->GetMoney()) * settings.maxBudgetPercent / 100.0 * (1.0 - 0.7 * traits.thrift));
        if (!budget)
            return false;

        AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(npc->GetFaction());
        if (!house)
            return false;

        bool const wantsBag = HasFreeBagSlot(bot);
        AuctionEntry const* best = nullptr;
        float bestGain = 0.0f;
        AuctionEntry const* bestBag = nullptr;
        uint32 scanned = 0;
        for (auto const& entry : house->GetAuctions())
        {
            AuctionEntry const* auction = entry.second;
            if (!auction || !auction->buyout || auction->buyout > budget || auction->owner == bot->GetGUID() ||
                auction->bidder == bot->GetGUID())
                continue;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
            if (!proto || proto->RequiredLevel > bot->GetLevel())
                continue;

            if (proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER && wantsBag)
            {
                if (!bestBag || auction->buyout < bestBag->buyout)
                    bestBag = auction;
                continue;
            }

            if ((proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR) ||
                bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;
            if (++scanned > settings.maxScan)
                break;

            Item* item = sAuctionMgr->GetAItem(auction->item_guid);
            float const gain = CoaGearScore::UpgradeGain(bot, proto->ItemId, item ? item->GetItemRandomPropertyId() : 0);
            if (gain > bestGain)
            {
                bestGain = gain;
                best = auction;
            }
        }

        AuctionEntry const* pick = best ? best : bestBag;
        if (!pick)
            return false;

        WorldPacket packet(CMSG_AUCTION_PLACE_BID);
        packet << auctioneer << uint32(pick->Id) << uint32(pick->buyout);
        bot->GetSession()->HandleAuctionPlaceBid(packet);
        return true;
    }

    void RunVisit(Visit const& visit)
    {
        Player* bot = ObjectAccessor::FindPlayer(visit.bot);
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat())
            return;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return;
        // The core's handlers check the range again; a bot that walked off simply does nothing.
        Creature* npc = bot->GetNPCIfCanInteractWith(visit.auctioneer, UNIT_NPC_FLAG_AUCTIONEER);
        if (!npc)
            return;

        Traits const traits = TraitsFor(bot->GetGUID());
        CollectAuctionMail(bot);
        ListLoot(bot, botAI, visit.auctioneer, traits);
        BuyOne(bot, visit.auctioneer, npc, traits);
    }

    void ProcessQueue()
    {
        for (uint32 i = 0; i < settings.visitsPerTick; ++i)
        {
            Visit visit;
            {
                std::lock_guard<std::mutex> guard(queueLock);
                if (queue.empty())
                    return;
                visit = queue.front();
                queue.pop_front();
            }
            RunVisit(visit);
        }
    }

    class CoaBotAuctionsWorld : public WorldScript
    {
    public:
        CoaBotAuctionsWorld() : WorldScript("CoaBotAuctionsWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) { }

        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        // World thread, between two map updates: no bot is being updated meanwhile.
        void OnUpdate(uint32 /*diff*/) override { ProcessQueue(); }
    };
}

bool IsEnabled()
{
    return settings.enabled;
}

Traits TraitsFor(ObjectGuid guid)
{
    Traits traits;
    traits.thrift = Hash01(guid, 1);
    traits.patience = Hash01(guid, 2);
    return traits;
}

bool IsOnCooldown(ObjectGuid bot)
{
    uint32 const now = uint32(GameTime::GetGameTime().count());
    std::lock_guard<std::mutex> guard(queueLock);
    auto found = lastVisit.find(bot.GetCounter());
    return found != lastVisit.end() && now < found->second + settings.cooldownSeconds;
}

bool RequestVisit(Player* bot, ObjectGuid auctioneer)
{
    if (!settings.enabled || !bot)
        return false;
    uint32 const now = uint32(GameTime::GetGameTime().count());
    std::lock_guard<std::mutex> guard(queueLock);
    uint32& last = lastVisit[bot->GetGUID().GetCounter()];
    if (last && now < last + settings.cooldownSeconds)
        return false;
    if (queue.size() >= MaxQueue)
        return false;
    last = now;
    queue.push_back({bot->GetGUID(), auctioneer});
    return true;
}

} // namespace CoaEconomy::BotAuctions

void AddSC_coa_economy_bot_auctions()
{
    // Off by default; read at start-up (restart to change it). The settings below it are read again on reload.
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Enable", false, false))
        return;
    CoaEconomy::BotAuctions::LoadSettings();
    new CoaEconomy::BotAuctions::CoaBotAuctionsWorld();
}
