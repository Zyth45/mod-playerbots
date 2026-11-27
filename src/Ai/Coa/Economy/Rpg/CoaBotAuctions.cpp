/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotAuctions.h"

#include "CoaEconomyConf.h"

#include "AuctionHouseMgr.h"
#include "CoaArtisans.h"
#include "CoaAuctionHouseBot.h"
#include "CoaCraftOrders.h"
#include "CoaEconomyRules.h"
#include "CoaGearScore.h"
#include "CoaMarketPrices.h"
#include "CoaMaterials.h"
#include "CoaMissionMgr.h"
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
#include "RandomPlayerbotMgr.h"
#include "World.h"
#include "PlayerbotFactory.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <map>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
        uint32 maxPerItem = 10;
        uint32 maxPerUpgradeKit = 3;
        bool realHousePrices = false;
        bool buyLog = false;  // 3.2: BotAuctions.BuyLog
        bool shopEnable = true;           // 3.2: BotAuctions.Shop.Enable
        uint32 shopCopperPerLevel = 200;  // BotAuctions.Shop.MinCopperPerLevel
        uint32 shopIntervalSeconds = 60 * MINUTE;  // BotAuctions.Shop.IntervalMinutes
        // 3.2: buying and selling from afar (CoaEconomy.RemoteBuy.*, RemoteSell.*), random bots only.
        bool remoteBuy = true;
        uint32 remoteBuySeconds = 60 * MINUTE;
        bool remoteSell = true;
        uint32 remoteSellSeconds = 120 * MINUTE;
        uint32 remoteSellMax = 2;
        uint32 remoteMaxBots = 20;  // RemoteTrade.MaxBotsPerPass: bots served per pass (one pass every 30 s)
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
        settings.enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Enable", true, false);
        settings.cooldownSeconds = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.VisitCooldownMinutes", 45) * MINUTE;
        settings.listPerVisit = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.ListPerVisit", 3);
        settings.maxBudgetPercent = std::min<uint32>(100, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.MaxBudgetPercent", 30));
        settings.visitsPerTick = std::max<uint32>(1, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.VisitsPerTick", 2));
        uint32 const hours = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.DurationHours", 24);
        settings.durationMinutes = (hours <= 12 ? 12 : hours <= 24 ? 24 : 48) * 60;
        settings.maxScan = std::max<uint32>(10, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.MaxScan", 300));
        settings.maxPerItem = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.MaxPerItem", 10);
        settings.maxPerUpgradeKit = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.MaxPerUpgradeKit", 3);
        settings.realHousePrices = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.RealHousePrices", true);
        settings.multipliers = RealPrices::LoadMultipliers("AiPlayerbot.CoaEconomy.BotAuctions.RealPriceMultiplier");
        settings.buyLog = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.BuyLog", false, false);
        settings.shopEnable = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Shop.Enable", true, false);
        settings.shopCopperPerLevel =
            CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.Shop.MinCopperPerLevel", 200, false);
        settings.shopIntervalSeconds = std::max<uint32>(5,
            CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.BotAuctions.Shop.IntervalMinutes", 60, false)) * MINUTE;
        settings.remoteBuy = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.RemoteBuy.Enable", true, false);
        settings.remoteBuySeconds = std::max<uint32>(5,
            CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.RemoteBuy.IntervalMinutes", 60, false)) * MINUTE;
        settings.remoteSell = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.RemoteSell.Enable", true, false);
        settings.remoteSellSeconds = std::max<uint32>(5,
            CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.RemoteSell.IntervalMinutes", 120, false)) * MINUTE;
        settings.remoteSellMax = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.RemoteSell.MaxItems", 2, false);
        settings.remoteMaxBots = std::max<uint32>(1,
            CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.RemoteTrade.MaxBotsPerPass", 20, false));
    }

    // 3.2 (BotAuctions.BuyLog): what the visits bought, self-found and classic bots apart; world thread only.
    struct BuyCounters
    {
        uint32 visits[2] = {0, 0}, noBudget[2] = {0, 0}, nothing[2] = {0, 0}, upgrades[2] = {0, 0}, bags[2] = {0, 0};
        uint64 spent[2] = {0, 0};
    } buyCounters;
    uint32 buySummaryAt = 0;

    // 3.2 (BuyLog): the way to a visit. Requests and refusals on the map threads, the visit's exits on the world thread.
    struct PathCounters
    {
        std::atomic<uint32> requests{0}, cooldown{0}, queueFull{0}, queued{0}, shopWants{0};
        std::atomic<uint32> sought{0}, picked{0}, station{0}, givenUp{0}, timedOut{0};
        std::atomic<uint32> leftFor[12]{};  // 0..10: NewRpgStatus of the next activity, 11: a reset
        std::mutex giveUpLock;
        std::map<std::string, std::array<uint32, 3>> giveUps;  // capital -> gone, no way, refusals
        std::map<std::string, uint32> noWayReasons;
        std::map<std::string, std::vector<std::string>> noWaySamples;  // capital -> at most NO_WAY_SAMPLES an hour
        std::map<std::string, uint32> notReached;                      // capital -> wanders ended short of it
        // 3.2.2: CoaSafeMove::Unstick of the "no way" give-ups: 0 path reaches the known stop, 1 walk, 2 teleport,
        // 3 given up, 4 no known stop.
        std::atomic<uint32> unstick[5]{};
        std::map<std::string, std::vector<std::string>> notReachedSamples;
        std::atomic<uint32> shopChecks{0}, shopPoor{0}, shopInterval{0};
        // 3.2: budget and threshold of the "too poor" checks, for their medians (a sample, at most POOR_SAMPLES).
        std::mutex poorLock;
        std::vector<std::pair<uint64, uint64>> poor;
        uint32 gone = 0, busy = 0, noAi = 0, outOfReach = 0, ran = 0;
        double outOfReachYards = 0.0;
    } pathCounters;

    // 3.2 (BuyLog): what the bots bought and listed from afar, and what the passes cost (world thread).
    struct RemoteCounters
    {
        uint32 buyTurns = 0, bought = 0, sellTurns = 0, listed = 0, vendorBetter = 0, passes = 0;
        uint64 spent = 0, listedValue = 0, passUs = 0, maxPassUs = 0;
        double gain = 0.0;
    } remoteCounters;
    uint32 remoteTimer = 0;
    std::unordered_map<ObjectGuid::LowType, uint32> nextRemoteBuy, nextRemoteSell;  // world thread

    void BuySummary()
    {
        uint32 const now = uint32(GameTime::GetGameTime().count());
        if (!buySummaryAt)
            buySummaryAt = now;
        if (now < buySummaryAt + 3600)
            return;
        buySummaryAt = now;
        if (settings.buyLog)
            LOG_INFO("playerbots.loot", "auction visits (last hour): {} auctioneers picked by a wander ({} by City Life, "
                     "{} sought for business), {} given up on the way, {} not reached before the wander ended; "
                     "{} asked ({} for shopping), {} refused on cooldown, "
                     "{} refused on a full queue, {} queued; {} ran, {} bots gone, {} dead or fighting, {} without AI, "
                     "{} out of the auctioneer's reach (average {:.1f} yd)",
                     pathCounters.picked.exchange(0), pathCounters.station.exchange(0), pathCounters.sought.exchange(0),
                     pathCounters.givenUp.exchange(0), pathCounters.timedOut.exchange(0), pathCounters.requests.exchange(0), pathCounters.shopWants.exchange(0),
                     pathCounters.cooldown.exchange(0), pathCounters.queueFull.exchange(0),
                     pathCounters.queued.exchange(0), pathCounters.ran, pathCounters.gone, pathCounters.busy,
                     pathCounters.noAi, pathCounters.outOfReach,
                     pathCounters.outOfReach ? pathCounters.outOfReachYards / pathCounters.outOfReach : 0.0);
        else
        {
            pathCounters.requests = 0;
            pathCounters.shopWants = 0;
            pathCounters.cooldown = 0;
            pathCounters.queueFull = 0;
            pathCounters.queued = 0;
            pathCounters.sought = pathCounters.picked = pathCounters.station = pathCounters.givenUp = 0;
            pathCounters.timedOut = 0;
        }
        if (settings.buyLog)
        {
            static char const* const names[12] = {"idle", "grind", "camp", "wander", "another NPC wander", "quest",
                                                  "flight", "rest", "outdoor pvp", "city", "status 10", "reset"};
            std::string left;
            uint32 total = 0;
            for (uint32 i = 0; i < 12; ++i)
                if (uint32 const n = pathCounters.leftFor[i].exchange(0))
                {
                    total += n;
                    left += (left.empty() ? "" : ", ") + std::string(names[i]) + " " + std::to_string(n);
                }
            std::vector<uint64> budgets, thresholds;
            {
                std::lock_guard<std::mutex> guard(pathCounters.poorLock);
                for (auto const& [budget, threshold] : pathCounters.poor)
                {
                    budgets.push_back(budget);
                    thresholds.push_back(threshold);
                }
                pathCounters.poor.clear();
            }
            auto median = [](std::vector<uint64>& v) -> uint64
            {
                if (v.empty())
                    return 0;
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                return v[v.size() / 2];
            };
            std::string ways;
            {
                std::lock_guard<std::mutex> guard(pathCounters.giveUpLock);
                for (auto const& [capital, n] : pathCounters.giveUps)
                    ways += (ways.empty() ? "" : "; ") + (capital.empty() ? std::string("outside a capital") : capital) +
                            " " + std::to_string(n[0]) + " gone, " + std::to_string(n[1]) + " no way, " +
                            std::to_string(n[2]) + " refused moves";
                pathCounters.giveUps.clear();
                std::string reasons;
                for (auto const& [why, n] : pathCounters.noWayReasons)
                    reasons += (reasons.empty() ? "" : ", ") + why + " " + std::to_string(n);
                auto bySite = [](std::map<std::string, std::vector<std::string>> const& samples)
                {
                    std::string text;
                    for (auto const& [capital, list] : samples)
                    {
                        text += (text.empty() ? "" : " / ") + (capital.empty() ? std::string("outside a capital") : capital) + ":";
                        for (std::string const& sample : list)
                            text += " " + sample + ";";
                    }
                    return text;
                };
                if (!reasons.empty())
                    ways += " | no way: " + reasons + " | samples: " + bySite(pathCounters.noWaySamples);
                std::string notReached;
                for (auto const& [capital, n] : pathCounters.notReached)
                    notReached += (notReached.empty() ? "" : ", ") +
                                  (capital.empty() ? std::string("outside a capital") : capital) + " " + std::to_string(n);
                if (!notReached.empty())
                    ways += " | not reached: " + notReached + " | samples: " + bySite(pathCounters.notReachedSamples);
                ways += " | off a navmesh island: " + std::to_string(pathCounters.unstick[1].exchange(0)) +
                        " walked, " + std::to_string(pathCounters.unstick[2].exchange(0)) + " teleported unseen, " +
                        std::to_string(pathCounters.unstick[3].exchange(0)) + " given up (a player in sight); not on one: " +
                        std::to_string(pathCounters.unstick[0].exchange(0)) + " whose path reaches the known stop, " +
                        std::to_string(pathCounters.unstick[4].exchange(0)) + " with no known stop";
                pathCounters.noWayReasons.clear();
                pathCounters.noWaySamples.clear();
                pathCounters.notReached.clear();
                pathCounters.notReachedSamples.clear();
            }
            LOG_INFO("playerbots.loot", "auction visits (last hour): auctioneers given up by capital: {}",
                     ways.empty() ? "none" : ways);
            LOG_INFO("playerbots.loot", "auction visits (last hour): {} wanders to an auctioneer left for another activity"
                     " ({}); shopping checks {}: {} too poor (median budget {} copper, median threshold {} copper), "
                     "{} visited within the interval",
                     total, left.empty() ? "none" : left, pathCounters.shopChecks.exchange(0),
                     pathCounters.shopPoor.exchange(0), median(budgets), median(thresholds),
                     pathCounters.shopInterval.exchange(0));
        }
        else
        {
            for (auto& n : pathCounters.leftFor)
                n = 0;
            pathCounters.shopChecks = pathCounters.shopPoor = pathCounters.shopInterval = 0;
        }
        pathCounters.gone = pathCounters.busy = pathCounters.noAi = pathCounters.outOfReach = pathCounters.ran = 0;
        pathCounters.outOfReachYards = 0.0;
        if (settings.buyLog)
            for (uint32 kind = 0; kind < 2; ++kind)
                LOG_INFO("playerbots.loot", "auction buys (last hour, {} bots): {} visits, {} without budget, {} with "
                         "nothing worth it, {} upgrades and {} bags bought for {}c", kind ? "self-found" : "classic",
                         buyCounters.visits[kind], buyCounters.noBudget[kind], buyCounters.nothing[kind],
                         buyCounters.upgrades[kind], buyCounters.bags[kind], buyCounters.spent[kind]);
        buyCounters = BuyCounters();
        if (settings.buyLog && (settings.remoteBuy || settings.remoteSell))
            LOG_INFO("playerbots.loot", "remote trade (last hour): {} buy turns, {} bought for {}c (gain {:.1f}); {} sell "
                     "turns, {} listed for {}c, {} kept for the vendor; {} passes, {:.2f} ms each on average, {:.2f} ms "
                     "at most", remoteCounters.buyTurns, remoteCounters.bought, remoteCounters.spent,
                     remoteCounters.gain, remoteCounters.sellTurns, remoteCounters.listed, remoteCounters.listedValue,
                     remoteCounters.vendorBetter, remoteCounters.passes,
                     remoteCounters.passes ? remoteCounters.passUs / 1000.0 / remoteCounters.passes : 0.0,
                     remoteCounters.maxPassUs / 1000.0);
        remoteCounters = RemoteCounters();
    }

    // The price a bot asks for one item: the market's learned price, else the house's, else a vendor-based guess.
    uint64 ReferencePrice(ItemTemplate const* proto)
    {
        // The real CoA auction house first (its market value, else its floor for the quality and level): our own
        // sales, made by bots at bots' prices, would keep a price low.
        if (settings.realHousePrices)
            if (uint64 real = Artisans::FairPrice(proto, settings.multipliers))
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
    // 3.2 (RemoteSell): what the core's CMSG_AUCTION_SELL_ITEM does for one whole stack (Handlers/AuctionHouseHandler
    // .cpp, the same on both cores), without the auctioneer: deposit, auction, item moved to the house, saved together.
    bool PostDirect(Player* bot, AuctionHouseObject* house, AuctionHouseEntry const* entry, AuctionHouseId houseId,
                    Item* item, uint64 bid, uint64 buyout)
    {
        if (!bid || bid > MAX_MONEY_AMOUNT || buyout > MAX_MONEY_AMOUNT)
            return false;
        if (sAuctionMgr->GetAItem(item->GetGUID()) || !item->CanBeTraded() || item->IsNotEmptyBag() ||
            item->GetTemplate()->HasFlag(ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION))
            return false;
        uint32 const etime = settings.durationMinutes * MINUTE;
        uint32 const deposit = sAuctionMgr->GetAuctionDeposit(entry, etime, item, item->GetCount());
        if (!bot->HasEnoughMoney(deposit))
            return false;
        bot->ModifyMoney(-int32(deposit));

        AuctionEntry* auction = new AuctionEntry;
        auction->Id = sObjectMgr->GenerateAuctionID();
        auction->houseId = houseId;
        auction->item_guid = item->GetGUID();
        auction->item_template = item->GetEntry();
        auction->itemCount = item->GetCount();
        auction->owner = bot->GetGUID();
        auction->startbid = uint32(bid);
        auction->bidder = ObjectGuid::Empty;
        auction->bid = 0;
        auction->buyout = uint32(buyout);
        auction->expire_time = GameTime::GetGameTime().count() + uint32(etime * sWorld->getRate(RATE_AUCTION_TIME));
        auction->deposit = deposit;
        auction->auctionHouseEntry = entry;
        sAuctionMgr->AddAItem(item);
        house->AddAuction(auction);
        bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        item->DeleteFromInventoryDB(trans);
        item->SaveToDB(trans);
        auction->SaveToDB(trans);
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
        return true;
    }

    // Where a bot away from any auctioneer trades: its faction's house (the neutral one on a realm that joins them).
    struct RemoteHouse
    {
        AuctionHouseObject* house = nullptr;
        AuctionHouseEntry const* entry = nullptr;
        AuctionHouseId id = AuctionHouseId::Neutral;
    };
    RemoteHouse RemoteHouseOf(Player* bot)
    {
        RemoteHouse remote;
        remote.id = sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION) ? AuctionHouseId::Neutral
                  : bot->GetTeamId() == TEAM_ALLIANCE                           ? AuctionHouseId::Alliance
                                                                                : AuctionHouseId::Horde;
        remote.house = sAuctionMgr->GetAuctionsMapByHouseId(remote.id);
        remote.entry = AuctionHouseMgr::GetAuctionHouseEntryFromHouse(remote.id);
        return remote;
    }

    uint32 ListLoot(Player* bot, PlayerbotAI* botAI, ObjectGuid auctioneer, Creature* npc, Traits const& traits,
                    Item* crafted, Item* held, RemoteHouse const* remote = nullptr)
    {
        uint32 const maxItems = remote ? settings.remoteSellMax : settings.listPerVisit;
        std::vector<Item*> loot;
        std::unordered_set<Item*> surplusItems;  // 3.1.4: stock materials beyond the stock, logged when listed
        std::unordered_set<uint32> const stock = Materials::StockReagents(bot);
        std::unordered_set<uint32> const enchanting = Artisans::EnchantReagents(bot);  // 3.2.2

        // Auctions per item already in this house: a bot adds one only while there are fewer than MaxPerItem of it
        // (MaxPerUpgradeKit for an upgrade kit).
        std::unordered_map<uint32, uint32> listed;
        if (AuctionHouseObject* house = remote ? remote->house : npc ? sAuctionMgr->GetAuctionsMap(npc->GetFaction()) : nullptr)
            for (auto const& [id, auction] : house->GetAuctions())
                if (auction)
                    ++listed[auction->item_template];
        auto underCap = [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            uint32 const cap = CoaEconomyRules::ItemCap(proto->Class, proto->SubClass, settings.maxPerItem,
                                                        settings.maxPerUpgradeKit);
            if (!CoaEconomyRules::UnderItemCap(listed[proto->ItemId], cap))
                return false;
            ++listed[proto->ItemId];
            return true;
        };

        if (crafted && underCap(crafted))
            loot.push_back(crafted);
        ForEachBagItem(bot, [&](Item* item)
        {
            if (item == crafted || item == held)
                return;
            if (loot.size() >= maxItems || item->IsSoulBound() || item->IsInTrade())
                return;
            // Never its ammunition, the consumables it uses, a vendor good or an item CoA does not have.
            if (!Sellable::BotMayList(item->GetTemplate(), false))
                return;
            // An artisan keeps the reagents of its recipes (Artisans.Stock), and the guild stock its kinds.
            if (Materials::KeepsInStock(bot, item, stock))
                return;
            // 3.2.2: an enchanter keeps one stack of each reagent of its enchantments in its bags.
            if (Artisans::KeepsForEnchanting(bot, item, enchanting))
                return;
            // 3.1.4: beyond that stock, a material of its stock is surplus, sold whatever its use for a profession.
            bool const surplus = stock.count(item->GetEntry()) != 0;
            if ((surplus || botAI->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get() ==
                                ITEM_USAGE_AH) &&
                underCap(item))
            {
                loot.push_back(item);
                if (surplus)
                    surplusItems.insert(item);
            }
        });
        if (crafted && !loot.empty() && loot.front() == crafted && !Sellable::BotMayList(crafted->GetTemplate(), true))
            loot.erase(loot.begin());

        // An impatient bot undercuts more: 85 % to 105 % of the reference price.
        for (Item* item : loot)
        {
            uint64 const buyout = std::min<uint64>(MAX_MONEY_AMOUNT,
                CoaEconomyRules::BotListingPrice(ReferencePrice(item->GetTemplate()), traits.patience, item->GetCount()));
            uint64 const bid = std::max<uint64>(1, buyout * 3 / 4);

            if (remote)
            {
                // From afar only what the house pays better than the vendor, after its cut.
                uint64 const vendor = uint64(item->GetTemplate()->SellPrice) * item->GetCount();
                if (!CoaEconomyRules::RemoteSellBeatsVendor(buyout, remote->entry->cutPercent, vendor))
                {
                    ++remoteCounters.vendorBetter;
                    continue;
                }
                if (PostDirect(bot, remote->house, remote->entry, remote->id, item, bid, buyout))
                {
                    ++remoteCounters.listed;
                    remoteCounters.listedValue += buyout;
                }
                continue;
            }
            WorldPacket packet(CMSG_AUCTION_SELL_ITEM);
            packet << auctioneer << uint32(1) << item->GetGUID() << uint32(item->GetCount())
                   << uint32(bid) << uint32(buyout) << uint32(settings.durationMinutes);
            if (surplusItems.count(item))
                LOG_INFO("playerbots.coa", "CoaEconomy: {} listed surplus {} x{} for {} copper", bot->GetName(),
                         item->GetEntry(), item->GetCount(), buyout);
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
    // 3.2 (RemoteBuy): what the core's CMSG_AUCTION_PLACE_BID does for a buyout (Handlers/AuctionHouseHandler.cpp, the
    // same on both cores), without the auctioneer: the seller is paid and the item comes by mail, at once.
    bool BuyoutDirect(Player* bot, AuctionHouseObject* house, AuctionEntry* auction)
    {
        if (!sScriptMgr->OnPlayerCanPlaceAuctionBid(bot, auction) || auction->owner == bot->GetGUID() ||
            sCharacterCache->GetCharacterAccountIdByGuid(auction->owner) == bot->GetSession()->GetAccountId() ||
            !auction->buyout || !bot->HasEnoughMoney(auction->buyout))
            return false;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (bot->GetGUID() == auction->bidder)
            bot->ModifyMoney(-int32(auction->buyout - auction->bid));
        else
        {
            bot->ModifyMoney(-int32(auction->buyout));
            if (auction->bidder)
                sAuctionMgr->SendAuctionOutbiddedMail(auction, auction->buyout, bot, trans);
        }
        auction->bidder = bot->GetGUID();
        auction->bid = auction->buyout;
        sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
        sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
        sAuctionMgr->SendAuctionWonMail(auction, trans);
        sScriptMgr->OnAuctionSuccessful(house, auction);
        auction->DeleteFromDB(trans);
        sAuctionMgr->RemoveAItem(auction->item_guid);
        house->RemoveAuction(auction);
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
        return true;
    }

    bool BuyOne(Player* bot, ObjectGuid auctioneer, AuctionHouseObject* house, Traits const& traits, bool remote = false)
    {
        // A thrifty bot spends down to 30 % of its share.
        uint64 const budget = CoaEconomyRules::BotBudget(bot->GetMoney(), settings.maxBudgetPercent, traits.thrift);
        uint32 const kind = PlayerbotFactory::SelfFoundAt(bot->GetLevel()) || PlayerbotFactory::IsSelfFoundBot(bot) ? 1 : 0;
        if (!remote)
            ++buyCounters.visits[kind];
        if (!budget)
        {
            if (!remote)
                ++buyCounters.noBudget[kind];
            return false;
        }
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
        if (remote)
        {
            // From afar: an upgrade only, worn at once (a bag waits for a visit).
            if (!best)
                return false;
            ObjectGuid const itemGuid = best->item_guid;
            uint32 const entry = best->item_template, price = best->buyout;
            if (!BuyoutDirect(bot, house, const_cast<AuctionEntry*>(best)))
                return false;
            ++remoteCounters.bought;
            remoteCounters.spent += price;
            remoteCounters.gain += bestGain;
            CollectAuctionMail(bot);
            Item* item = bot->GetItemByGuid(itemGuid);
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            uint8 const slot = item && botAI ? botAI->FindEquipSlot(item->GetTemplate(), NULL_SLOT, true) : uint8(NULL_SLOT);
            if (slot != NULL_SLOT)
                CoaMissionMgr::EquipItem(bot, item, slot);
            if (settings.buyLog)
                LOG_INFO("playerbots.loot", "remote buy: {} (level {}, {}, budget {}c) bought {} for {}c (gain {:.1f}){}",
                         bot->GetName(), bot->GetLevel(), kind ? "self-found" : "classic", budget, entry, price,
                         bestGain, slot != NULL_SLOT ? ", worn" : (item ? ", kept" : ", in the mail"));
            return true;
        }
        if (!pick)
        {
            ++buyCounters.nothing[kind];
            return false;
        }
        ++(best ? buyCounters.upgrades[kind] : buyCounters.bags[kind]);
        buyCounters.spent[kind] += pick->buyout;
        if (settings.buyLog)
            LOG_INFO("playerbots.loot", "auction buy: {} (level {}, {}, {}c, budget {}c) bought {} for {}c ({}, gain {:.1f})",
                     bot->GetName(), bot->GetLevel(), kind ? "self-found" : "classic", bot->GetMoney(), budget,
                     pick->item_template, pick->buyout, best ? "an upgrade" : "a bag", best ? bestGain : 0.0f);

        WorldPacket packet(CMSG_AUCTION_PLACE_BID);
        packet << auctioneer << uint32(pick->Id) << uint32(pick->buyout);
        bot->GetSession()->HandleAuctionPlaceBid(packet);
        return true;
    }

    void RunVisit(Visit const& visit)
    {
        Player* bot = ObjectAccessor::FindPlayer(visit.bot);
        if (!bot || !bot->IsInWorld())
        {
            ++pathCounters.gone;
            return;
        }
        if (!bot->IsAlive() || bot->IsInCombat())
        {
            ++pathCounters.busy;
            return;
        }
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
        {
            ++pathCounters.noAi;
            return;
        }
        // The core's handlers check the range again; a bot that walked off simply does nothing.
        Creature* npc = bot->GetNPCIfCanInteractWith(visit.auctioneer, UNIT_NPC_FLAG_AUCTIONEER);
        if (!npc)
        {
            ++pathCounters.outOfReach;
            if (Creature* there = ObjectAccessor::GetCreature(*bot, visit.auctioneer))
                pathCounters.outOfReachYards += bot->GetDistance(there);
            return;
        }
        ++pathCounters.ran;

        Traits const traits = TraitsFor(bot->GetGUID());
        LOG_DEBUG("playerbots.coa", "CoaEconomy: {} visits the auctioneer", bot->GetName());
        CollectAuctionMail(bot);

        // Ore smelted or prospected, gear disenchanted, when what comes out is worth more (Materials).
        Materials::Process(bot, botAI);
        // Guild stock: intermediates from the bot's own materials, and the extra full stacks to its bank.
        Materials::TendGuildStock(bot);
        // 3.2: its hourly study (craft or enchant for itself, missing reagents bought), when due; else, with
        // SelfStudy off, what it can make with the reagents it holds.
        if (!Artisans::StudyIfDue(bot, botAI))
        {
            Artisans::CraftUpgradeForSelf(bot, botAI);
            Artisans::EnchantForSelf(bot);
        }

        // An artisan makes a piece worth more than its materials and puts it up with its loot.
        Item* crafted = nullptr;
        Artisans::Recipe recipe;
        if (Artisans::IsEnabled() && Artisans::PickProfitable(bot, recipe))
            crafted = Artisans::Craft(bot, recipe);

        // Sometimes offered in Trade first (CoA Live's offer: a player who answers gets it COD), else listed here.
        Item* held = nullptr;
        if (crafted && CraftOrders::OfferInTrade(bot, crafted, recipe.productValue))
            std::swap(crafted, held);

        ListLoot(bot, botAI, visit.auctioneer, npc, traits, crafted, held);
        // One purchase per visit: an upgrade or a bag, else a cheap lot for the artisan's stock.
        if (!BuyOne(bot, visit.auctioneer, sAuctionMgr->GetAuctionsMap(npc->GetFaction()), traits))
            Materials::BuyStock(bot, visit.auctioneer, npc,
                CoaEconomyRules::BotBudget(bot->GetMoney(), settings.maxBudgetPercent, traits.thrift));
    }

    // A bot's turn: false until its first turn (spread over the interval) or its next one comes.
    bool RemoteDue(std::unordered_map<ObjectGuid::LowType, uint32>& next, ObjectGuid guid, uint32 now,
                   uint32 interval, uint32 salt)
    {
        auto it = next.find(guid.GetCounter());
        if (it == next.end())
        {
            next[guid.GetCounter()] = CoaEconomyRules::FirstRemoteTurn(now, Hash01(guid, salt), interval);
            return false;
        }
        if (now < it->second)
            return false;
        it->second = now + interval;
        return true;
    }

    // 3.2: every 30 s, at most RemoteTrade.MaxBotsPerPass random bots whose turn has come buy (RemoteBuy) or list
    // (RemoteSell) at their faction's house from where they are; never a player's bot or alt, never in combat, in a
    // dungeon or a battleground. A bot left out by the cap keeps its turn for the next pass.
    void RemoteTrade(uint32 diff)
    {
        if (!settings.remoteBuy && !settings.remoteSell)
            return;
        remoteTimer += diff;
        if (remoteTimer < 30 * IN_MILLISECONDS)
            return;
        remoteTimer = 0;
        auto const start = std::chrono::steady_clock::now();
        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 served = 0;
        for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
        {
            if (served >= settings.remoteMaxBots)
                break;
            if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsInFlight() || !bot->IsAlive() ||
                bot->IsInCombat() || bot->GetMap()->Instanceable() || !sRandomPlayerbotMgr.IsRandomBot(bot))
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI || !botAI->IsBotAI())
                continue;
            bool const buy = settings.remoteBuy && RemoteDue(nextRemoteBuy, bot->GetGUID(), now, settings.remoteBuySeconds, 3);
            bool const sell = settings.remoteSell && !sCoaMissionMgr.IsOnMission(bot) &&
                              RemoteDue(nextRemoteSell, bot->GetGUID(), now, settings.remoteSellSeconds, 4);
            if (!buy && !sell)
                continue;
            RemoteHouse const remote = RemoteHouseOf(bot);
            if (!remote.house || !remote.entry)
                continue;
            ++served;
            Traits const traits = TraitsFor(bot->GetGUID());
            CollectAuctionMail(bot);
            if (sell)
            {
                ++remoteCounters.sellTurns;
                ListLoot(bot, botAI, ObjectGuid::Empty, nullptr, traits, nullptr, nullptr, &remote);
            }
            if (buy)
            {
                ++remoteCounters.buyTurns;
                BuyOne(bot, ObjectGuid::Empty, remote.house, traits, true);
            }
        }
        uint64 const us = uint64(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
        ++remoteCounters.passes;
        remoteCounters.passUs += us;
        remoteCounters.maxPassUs = std::max(remoteCounters.maxPassUs, us);
    }

    void ProcessQueue(uint32 diff)
    {
        BuySummary();
        RemoteTrade(diff);
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
        // A bot's auction under half the real house's price goes back too, to be listed again at the right price:
        // random bots with BotAuctions.RealHousePrices, the house bot with its own key.
        bool const houseBotReal = CoaEconomy::Conf::Get<bool>("AuctionHouseBot.CoA.RealHousePrices", false);
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
                    uint64 const real = Artisans::FairPrice(proto, houseBot ? houseBotMultipliers : settings.multipliers);
                    if (real && auction->buyout && auction->itemCount &&
                        CoaEconomyRules::OutOfFairRange(auction->buyout / auction->itemCount, real))
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
            LOG_INFO("server.loading", "CoA Economy: {} bots' auctions went back to them ({} out of the "
                     "fair price: under half or over twice it; the others items a bot may not list)", taken, underpriced);
    }

    class CoaBotAuctionsWorld : public WorldScript
    {
    public:
        CoaBotAuctionsWorld()
            : WorldScript("CoaBotAuctionsWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE}) { }

        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        void OnStartup() override { TakeBackForbiddenListings(); }
        // World thread, between two map updates: no bot is being updated meanwhile.
        void OnUpdate(uint32 diff) override { ProcessQueue(diff); }
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
    ++pathCounters.requests;
    std::lock_guard<std::mutex> guard(queueLock);
    uint32& last = lastVisit[bot->GetGUID().GetCounter()];
    if (last && now < last + settings.cooldownSeconds)
    {
        ++pathCounters.cooldown;
        return false;
    }
    if (queue.size() >= MaxQueue)
    {
        ++pathCounters.queueFull;
        return false;
    }
    last = now;
    queue.push_back({bot->GetGUID(), auctioneer});
    ++pathCounters.queued;
    return true;
}

void NoteWay(Way way)
{
    if (!settings.buyLog)
        return;
    switch (way)
    {
        case Way::Sought: ++pathCounters.sought; break;
        case Way::Picked: ++pathCounters.picked; break;
        case Way::Station: ++pathCounters.station; break;
        case Way::GivenUp: ++pathCounters.givenUp; break;
        case Way::TimedOut: ++pathCounters.timedOut; break;
    }
}

void NoteLeftFor(int nextStatus)
{
    if (!settings.buyLog)
        return;
    uint32 const slot = nextStatus >= 0 && nextStatus <= 10 ? uint32(nextStatus) : 11;
    ++pathCounters.leftFor[slot];
}

void NoteGiveUp(int why, char const* capital)
{
    if (!settings.buyLog || why < 1 || why > 3)
        return;
    std::lock_guard<std::mutex> guard(pathCounters.giveUpLock);
    if (pathCounters.giveUps.size() < 64 || pathCounters.giveUps.count(capital ? capital : ""))
        ++pathCounters.giveUps[capital ? capital : ""][why - 1];
}

constexpr size_t SITE_SAMPLES = 8;  // per capital and hour
constexpr size_t SITES = 64;

void NoteNoWay(char const* capital, char const* reason, uint32 entry, float x, float y, float z, float gap)
{
    if (!settings.buyLog)
        return;
    std::lock_guard<std::mutex> guard(pathCounters.giveUpLock);
    std::string const why = reason ? reason : "";
    std::string const site = capital ? capital : "";
    ++pathCounters.noWayReasons[why];
    if (pathCounters.noWaySamples.size() >= SITES && !pathCounters.noWaySamples.count(site))
        return;
    std::vector<std::string>& samples = pathCounters.noWaySamples[site];
    if (samples.size() < SITE_SAMPLES)
    {
        char sample[160];
        std::snprintf(sample, sizeof(sample), "entry %u from %.1f %.1f %.1f, %s (end %.1f yd off)", entry, x, y, z,
                      why.c_str(), gap);
        samples.emplace_back(sample);
    }
}

void NoteUnstick(int how)
{
    if (settings.buyLog && how >= 0 && how <= 4)
        ++pathCounters.unstick[how];
}

void NoteNotReached(char const* capital, uint32 entry, float x, float y, float z, float toNpc, bool moving,
                    uint32 refusals, uint32 ranSecondsAgo, bool inReach, char const* lastAction)
{
    if (!settings.buyLog)
        return;
    std::lock_guard<std::mutex> guard(pathCounters.giveUpLock);
    std::string const site = capital ? capital : "";
    if (pathCounters.notReached.size() >= SITES && !pathCounters.notReached.count(site))
        return;
    ++pathCounters.notReached[site];
    std::vector<std::string>& samples = pathCounters.notReachedSamples[site];
    if (samples.size() < SITE_SAMPLES)
    {
        char sample[256];
        std::snprintf(sample, sizeof(sample),
                      "entry %u from %.1f %.1f %.1f, %.1f yd off, %s, %u refused, wander ran %us ago, %s, last %s", entry,
                      x, y, z, toNpc, moving ? "moving" : "still", refusals, ranSecondsAgo,
                      inReach ? "in reach" : "out of reach", lastAction ? lastAction : "");
        samples.emplace_back(sample);
    }
}

void NoteShopNo(ShopNo why)
{
    if (!settings.buyLog)
        return;
    ++(why == ShopNo::Poor ? pathCounters.shopPoor : pathCounters.shopInterval);
}

bool WantsToShop(Player* bot)
{
    if (!settings.enabled || !settings.shopEnable || !bot)
        return false;
    if (settings.buyLog)
        ++pathCounters.shopChecks;
    uint64 const budget = CoaEconomyRules::BotBudget(bot->GetMoney(), settings.maxBudgetPercent, TraitsFor(bot->GetGUID()).thrift);
    if (!CoaEconomyRules::WantsToShop(budget, bot->GetLevel(), settings.shopCopperPerLevel))
    {
        NoteShopNo(ShopNo::Poor);
        if (settings.buyLog)
        {
            constexpr size_t POOR_SAMPLES = 4096;
            std::lock_guard<std::mutex> guard(pathCounters.poorLock);
            if (pathCounters.poor.size() < POOR_SAMPLES)
                pathCounters.poor.emplace_back(budget,
                                               CoaEconomyRules::ShopThreshold(bot->GetLevel(), settings.shopCopperPerLevel));
        }
        return false;
    }
    uint32 const now = uint32(GameTime::GetGameTime().count());
    std::lock_guard<std::mutex> guard(queueLock);
    auto found = lastVisit.find(bot->GetGUID().GetCounter());
    if (!CoaEconomyRules::ShopDue(now, found == lastVisit.end() ? 0 : found->second, settings.shopIntervalSeconds))
    {
        NoteShopNo(ShopNo::Interval);
        return false;
    }
    ++pathCounters.shopWants;
    return true;
}

bool HasPendingVisit(ObjectGuid bot)
{
    std::lock_guard<std::mutex> guard(queueLock);
    for (Visit const& visit : queue)
        if (visit.bot == bot)
            return true;
    return false;
}

} // namespace CoaEconomy::BotAuctions

void AddSC_coa_economy_bot_auctions()
{
    // Off by default; read at start-up (restart to change it). The settings below it are read again on reload.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.BotAuctions.Enable", true, false))
        return;
    CoaEconomy::BotAuctions::LoadSettings();
    new CoaEconomy::BotAuctions::CoaBotAuctionsWorld();
}
