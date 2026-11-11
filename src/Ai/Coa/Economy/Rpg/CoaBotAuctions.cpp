/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotAuctions.h"

#include "AuctionHouseMgr.h"
#include "CoaArtisans.h"
#include "CoaAuctionHouseBot.h"
#include "CoaCraftOrders.h"
#include "CoaEconomyRules.h"
#include "CoaGearScore.h"
#include "CoaMarketPrices.h"
#include "CoaMaterials.h"
#include "CoaRealPrices.h"
#include "CoaSellable.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
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
#include <unordered_set>

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
        bool realHousePrices = false;
        RealPrices::Multipliers multipliers;
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
        settings.realHousePrices = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.BotAuctions.RealHousePrices", false);
        settings.multipliers = RealPrices::LoadMultipliers("AiPlayerbot.CoaEconomy.BotAuctions.RealPriceMultiplier");
    }

    // The price a bot asks for one item: the market's learned price, else the house's, else a vendor-based guess.
    uint64 ReferencePrice(ItemTemplate const* proto)
    {
        // The real CoA auction house first (its market value, else its floor for the quality and level): our own
        // sales, made by bots at bots' prices, would keep a price low.
        if (settings.realHousePrices)
            if (uint64 real = RealPrices::Scaled(RealPrices::Priced(proto), proto, settings.multipliers))
                return real;
        uint64 price = std::max<uint64>(100, uint64(proto->SellPrice) * 4);
        if (MarketBand const* band = sCoaMarketPrices->GetBand(proto->ItemId); band && band->Usual)
            price = band->Usual;
        else if (uint64 house = auctionbot->GetReferencePricePerItem(proto))
            price = house;
        // Never under the real house's floor for its quality and level.
        return settings.realHousePrices
            ? std::max(price, RealPrices::Scaled(RealPrices::Floor(proto), proto, settings.multipliers)) : price;
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
    // crafted: a piece the artisan just made, listed first; held: a piece offered in Trade, kept for its buyer.
    uint32 ListLoot(Player* bot, PlayerbotAI* botAI, ObjectGuid auctioneer, Traits const& traits, Item* crafted, Item* held)
    {
        std::vector<Item*> loot;
        std::unordered_set<uint32> const stock = Materials::StockReagents(bot);
        if (crafted)
            loot.push_back(crafted);
        ForEachBagItem(bot, [&](Item* item)
        {
            if (item == crafted || item == held)
                return;
            if (loot.size() >= settings.listPerVisit || item->IsSoulBound() || item->IsInTrade())
                return;
            // Never its ammunition, the consumables it uses, a vendor good or an item CoA does not have.
            if (!Sellable::BotMayList(item->GetTemplate(), false))
                return;
            // An artisan keeps the reagents of its recipes (Artisans.Stock).
            if (Materials::KeepsInStock(bot, item, stock))
                return;
            if (botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get() == ITEM_USAGE_AH)
                loot.push_back(item);
        });
        if (crafted && !Sellable::BotMayList(crafted->GetTemplate(), true))
            loot.erase(loot.begin());

        // An impatient bot undercuts more: 85 % to 105 % of the reference price.
        for (Item* item : loot)
        {
            uint64 const buyout = std::min<uint64>(MAX_MONEY_AMOUNT,
                CoaEconomyRules::BotListingPrice(ReferencePrice(item->GetTemplate()), traits.patience, item->GetCount()));
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
        uint64 const budget = CoaEconomyRules::BotBudget(bot->GetMoney(), settings.maxBudgetPercent, traits.thrift);
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

        // Ore smelted or prospected, gear disenchanted, when what comes out is worth more (Materials).
        Materials::Process(bot, botAI);

        // An artisan makes a piece worth more than its materials and puts it up with its loot (lot 4).
        Item* crafted = nullptr;
        Artisans::Recipe recipe;
        if (Artisans::IsEnabled() && Artisans::PickProfitable(bot, recipe))
            crafted = Artisans::Craft(bot, recipe);

        // Sometimes offered in Trade first (CoA Live's offer: a player who answers gets it COD), else listed here.
        Item* held = nullptr;
        if (crafted && CraftOrders::OfferInTrade(bot, crafted, recipe.productValue))
            std::swap(crafted, held);

        ListLoot(bot, botAI, visit.auctioneer, traits, crafted, held);
        // One purchase per visit: an upgrade or a bag, else a cheap lot for the artisan's stock.
        if (!BuyOne(bot, visit.auctioneer, npc, traits))
            Materials::BuyStock(bot, visit.auctioneer, npc,
                CoaEconomyRules::BotBudget(bot->GetMoney(), settings.maxBudgetPercent, traits.thrift));
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

    // Start-up: a random bot's auction of what a bot may not list (ammunition, an item CoA does not have, a vendor
    // good) goes back to it by mail, as when it ends unsold; a bidder gets its money back. Listings made before the
    // rules (the auction clock keeps them alive across restarts) are cleaned this way; nothing is destroyed.
    void TakeBackForbiddenListings()
    {
        uint32 taken = 0;
        uint32 underpriced = 0;
        // A bot's auction under half the real house's price goes back too, to be listed again at the right price
        // (Pilote, 09/11): random bots with BotAuctions.RealHousePrices, the house bot with its own key.
        bool const houseBotReal = sConfigMgr->GetOption<bool>("AuctionHouseBot.CoA.RealHousePrices", false);
        uint32 const houseBotAccount = houseBotReal ? AccountMgr::GetId("AHBOT") : 0;
        RealPrices::Multipliers const houseBotMultipliers =
            RealPrices::LoadMultipliers("AuctionHouseBot.CoA.RealPriceMultiplier");
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (AuctionHouseId houseId : {AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral})
        {
            AuctionHouseObject* house = sAuctionMgr->GetAuctionsMapByHouseId(houseId);
            if (!house)
                continue;
            std::vector<AuctionEntry*> forbidden;
            for (auto const& [id, auction] : house->GetAuctions())
            {
                if (!auction)
                    continue;
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
                if (!proto)
                    continue;
                uint32 const account = sCharacterCache->GetCharacterAccountIdByGuid(auction->owner);
                bool const randomBot = sPlayerbotAIConfig.IsInRandomAccountList(account);
                bool const houseBot = houseBotAccount && account == houseBotAccount;
                if (!randomBot && !houseBot)
                    continue;
                if (randomBot && !Sellable::BotMayList(proto, true))
                {
                    forbidden.push_back(auction);
                    continue;
                }
                if ((randomBot && settings.realHousePrices) || houseBot)
                {
                    uint64 const real = RealPrices::Scaled(RealPrices::Reference(proto), proto,
                        houseBot ? houseBotMultipliers : settings.multipliers);
                    if (real && auction->buyout && auction->itemCount &&
                        CoaEconomyRules::UnderHalf(auction->buyout / auction->itemCount, real))
                    {
                        forbidden.push_back(auction);
                        ++underpriced;
                    }
                }
            }
            for (AuctionEntry* auction : forbidden)
            {
                if (auction->bidder)
                    sAuctionMgr->SendAuctionCancelledToBidderMail(auction, trans);
                sAuctionMgr->SendAuctionExpiredMail(auction, trans, false);
                auction->DeleteFromDB(trans);
                sAuctionMgr->RemoveAItem(auction->item_guid);
                house->RemoveAuction(auction);
                ++taken;
            }
        }
        CharacterDatabase.CommitTransaction(trans);
        if (taken)
            LOG_INFO("server.loading", "CoA Economy: {} bots' auctions went back to them ({} under half the real CoA "
                     "house's price, the others items a bot may not list)", taken, underpriced);
    }

    class CoaBotAuctionsWorld : public WorldScript
    {
    public:
        CoaBotAuctionsWorld()
            : WorldScript("CoaBotAuctionsWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE}) { }

        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        void OnStartup() override { TakeBackForbiddenListings(); }
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
