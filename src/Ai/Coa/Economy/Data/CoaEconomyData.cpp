/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEconomyData.h"

#include "CoaEconomyConf.h"

#include "AsyncCallbackProcessor.h"
#include "AuctionHouseMgr.h"
#include "CoaMarketPrices.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "QueryCallback.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace CoaEconomy::Data
{

namespace
{
    using Clock = std::chrono::steady_clock;
    constexpr uint32 Day = DAY;
    constexpr uint32 EverySeconds = 60;          // economy.json is rebuilt every minute while watched
    constexpr uint32 WatchCheckMs = 5000;        // watch.json's date is looked at every 5 s
    constexpr uint32 WatchFreshSeconds = 90;     // a watch older than this means nobody looks
    constexpr uint32 GoldRefreshSeconds = 600;   // gold totals: one async query every 10 min while watched
    constexpr std::size_t MaxOrders = 50;
    constexpr std::size_t TopSales = 10;

    // ---- What the bricks report (world thread) ----
    struct Sale { uint32 at; uint32 item; uint32 count; uint64 price; };
    struct Craft { uint32 at; uint32 skill; uint32 item; uint32 count; int64 profit; };
    struct Order { uint32 id; uint32 at; std::string buyer; std::string bot; uint32 item; uint32 price; std::string state;
                   uint32 skill; std::string kind; };
    struct Break { uint32 at; bool disenchant; uint32 item; uint32 count; uint32 from; };
    // Counters merged per minute, so a busy AHBot costs one entry a minute.
    struct AhBotTick { uint32 at; uint32 listed; uint32 bought; uint64 in; uint64 out; };
    struct Tick { uint32 at; uint32 n; };

    std::deque<Sale> sales;
    std::deque<Craft> crafts;
    std::deque<Order> orders;
    std::deque<Break> breaks;
    std::deque<AhBotTick> ahbot;
    std::deque<Tick> expired;
    std::deque<Tick> sold;
    uint32 nextOrderId = 1;

    // The auction clock (null in the file until it reports at start-up).
    bool clockKnown = false;
    uint32 clockOffline = 0;
    uint32 clockResumed = 0;

    constexpr uint32 SkillMining = 186;

    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }

    template <typename T>
    void Forget(std::deque<T>& list, uint32 now)
    {
        while (!list.empty() && list.front().at + Day < now)
            list.pop_front();
    }

    void Count(std::deque<Tick>& list, uint32 now)
    {
        if (!list.empty() && list.back().at / MINUTE == now / MINUTE)
            ++list.back().n;
        else
            list.push_back({now, 1});
    }

    uint32 Sum(std::deque<Tick> const& list)
    {
        uint32 total = 0;
        for (Tick const& tick : list)
            total += tick.n;
        return total;
    }

    // ---- Settings ----
    bool enabled = false;
    std::string dir;

    void LoadSettings()
    {
        enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.DataFile.Enable", true, false) &&
                  CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaDataFiles", true, false);
        dir = CoaEconomy::Conf::Get<std::string>("AiPlayerbot.CoaDataDir", "", false);
        if (dir.empty())
        {
            std::string logs = CoaEconomy::Conf::Get<std::string>("LogsDir", "", false);
            if (!logs.empty() && logs.back() != '/' && logs.back() != '\\')
                logs += '/';
            dir = logs + "squidbot";
        }
    }

    // ---- JSON helpers ----
    void Escape(std::string& out, std::string const& text)
    {
        out += '"';
        for (char c : text)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            if (uint8(c) < 0x20)
                continue;
            out += c;
        }
        out += '"';
    }

    // ---- Writer thread: only the newest snapshot waits; a slow disk loses snapshots, never a world update ----
    class Writer
    {
    public:
        ~Writer() { Stop(); }

        void Post(std::string path, std::string text)
        {
            {
                std::lock_guard<std::mutex> guard(lock);
                if (!thread.joinable())
                    thread = std::thread(&Writer::Run, this);
                pendingPath = std::move(path);
                pending = std::move(text);
                hasPending = true;
            }
            wake.notify_one();
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> guard(lock);
                stopping = true;
            }
            wake.notify_one();
            if (thread.joinable())
                thread.join();
        }

    private:
        void Run()
        {
            for (;;)
            {
                std::string text, path;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    wake.wait(guard, [this] { return hasPending || stopping; });
                    if (stopping)
                        return;
                    text.swap(pending);
                    path.swap(pendingPath);
                    hasPending = false;
                }
                Write(path, text);
            }
        }

        // A .tmp beside the file, renamed over it; a reader holding the file open makes the rename wait a moment.
        void Write(std::string const& path, std::string const& text)
        {
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
            std::string const tmp = path + ".tmp";
            {
                std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
                file.write(text.data(), std::streamsize(text.size()));
                if (!file)
                {
                    if (!failed)
                        LOG_ERROR("playerbots.coa", "CoA Economy: cannot write {}", tmp);
                    failed = true;
                    return;
                }
            }
            failed = false;
            for (uint32 attempt = 0; attempt < 50; ++attempt)
            {
                std::filesystem::rename(tmp, path, error);
                if (!error)
                    return;
                {
                    std::lock_guard<std::mutex> guard(lock);
                    if (hasPending || stopping)
                        return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }

        std::mutex lock;
        std::condition_variable wake;
        std::thread thread;
        std::string pending, pendingPath;
        bool hasPending = false;
        bool stopping = false;
        bool failed = false;
    };

    // ---- Snapshot ----
    struct CategoryStats
    {
        uint32 auctions = 0;
        uint64 priceSum = 0;   // per item, buyout auctions only
        uint32 priced = 0;
        uint64 low = 0, usual = 0, high = 0;
        uint32 banded = 0;
        uint32 volume = 0;     // items sold in 24 h
    };

    class Economy
    {
    public:
        void Update(uint32 diff)
        {
            _gold.ProcessReadyCallbacks();
            if (!enabled)
                return;

            _watchTimer += diff;
            if (_watchTimer < WatchCheckMs)
                return;
            _watchTimer = 0;
            if (!Watched())
                return;

            uint32 const now = Now();
            if (_lastGold + GoldRefreshSeconds <= now && !_goldPending)
                RefreshGold(now);
            if (_lastBuild + EverySeconds > now)
                return;
            _lastBuild = now;
            Build(now);
        }

        void Stop() { _writer.Stop(); }

    private:
        // The dashboard asks for "economy" in squidbot/watch.json, written less than 90 s ago.
        bool Watched()
        {
            std::error_code error;
            std::filesystem::path const watch = std::filesystem::path(dir) / "watch.json";
            auto const written = std::filesystem::last_write_time(watch, error);
            if (error)
                return false;
            if (written != _watchStamp)
            {
                _watchStamp = written;
                std::ifstream file(watch, std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                _economyWatched = text.find("\"economy\"") != std::string::npos;
            }
            auto const age = std::filesystem::file_time_type::clock::now() - written;
            return _economyWatched && age < std::chrono::seconds(WatchFreshSeconds);
        }

        // Gold of every character and of the random bots, one async query (never on the world tick).
        void RefreshGold(uint32 now)
        {
            _goldPending = true;
            _gold.AddCallback(CharacterDatabase.AsyncQuery("SELECT account, SUM(money) FROM characters GROUP BY account")
                .WithCallback([this, now](QueryResult result)
                {
                    _goldPending = false;
                    _lastGold = now;
                    _goldAll = 0;
                    _goldBots = 0;
                    if (!result)
                        return;
                    do
                    {
                        Field* fields = result->Fetch();
                        uint64 const money = fields[1].Get<uint64>();
                        _goldAll += money;
                        if (sPlayerbotAIConfig.IsInRandomAccountList(fields[0].Get<uint32>()))
                            _goldBots += money;
                    } while (result->NextRow());
                }));
        }

        void Build(uint32 now)
        {
            Clock::time_point const start = Clock::now();
            Forget(sales, now);
            Forget(crafts, now);
            Forget(orders, now);
            Forget(breaks, now);
            Forget(ahbot, now);
            Forget(expired, now);
            Forget(sold, now);

            // One pass over the three auction houses.
            std::map<std::pair<uint32, uint32>, CategoryStats> categories;
            uint32 auctions = 0;
            for (AuctionHouseId house : {AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral})
            {
                AuctionHouseObject* object = sAuctionMgr->GetAuctionsMapByHouseId(house);
                if (!object)
                    continue;
                for (auto const& entry : object->GetAuctions())
                {
                    AuctionEntry const* auction = entry.second;
                    ItemTemplate const* proto = auction ? sObjectMgr->GetItemTemplate(auction->item_template) : nullptr;
                    if (!proto)
                        continue;
                    ++auctions;
                    CategoryStats& stats = categories[{proto->Class, proto->SubClass}];
                    ++stats.auctions;
                    if (auction->buyout && auction->itemCount)
                    {
                        stats.priceSum += auction->buyout / auction->itemCount;
                        ++stats.priced;
                    }
                    if (MarketBand const* band = sCoaMarketPrices->GetBand(proto->ItemId))
                    {
                        stats.low += band->Low;
                        stats.usual += band->Usual;
                        stats.high += band->High;
                        ++stats.banded;
                    }
                }
            }

            // Sales of the last 24 h: volume per category and the best-selling items.
            std::unordered_map<uint32, std::pair<uint32, uint64>> byItem; // item -> count, copper
            for (Sale const& sale : sales)
            {
                auto& total = byItem[sale.item];
                total.first += sale.count;
                total.second += sale.price;
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(sale.item))
                    categories[{proto->Class, proto->SubClass}].volume += sale.count;
            }
            std::vector<std::pair<uint32, std::pair<uint32, uint64>>> top(byItem.begin(), byItem.end());
            std::sort(top.begin(), top.end(), [](auto const& a, auto const& b) { return a.second.second > b.second.second; });
            if (top.size() > TopSales)
                top.resize(TopSales);

            // Crafts of the last 24 h by profession and item.
            std::map<std::pair<uint32, uint32>, std::pair<uint32, int64>> crafted;
            for (Craft const& craft : crafts)
            {
                auto& total = crafted[{craft.skill, craft.item}];
                total.first += craft.count;
                total.second += craft.profit;
            }

            std::string out;
            out.reserve(4096 + categories.size() * 96);
            out += "{\"schema\":\"squidbot.economy/1\",\"at\":";
            out += std::to_string(uint64(time(nullptr)));
            out += ",\"every\":";
            out += std::to_string(EverySeconds);
            out += ",\"build_us\":";
            out += std::to_string(_lastBuildUs);
            out += ",\"server_start\":";
            out += std::to_string(uint64(GameTime::GetStartTime().count()));
            out += ",\"module\":null,\"data\":{\"auctions\":";
            out += std::to_string(auctions);
            out += ",\"gold_circulating\":";
            out += _lastGold ? std::to_string(_goldAll) : "null";
            out += ",\"gold_bots\":";
            out += _lastGold ? std::to_string(_goldBots) : "null";

            out += ",\"categories\":[";
            bool first = true;
            for (auto const& [key, stats] : categories)
            {
                if (!stats.auctions && !stats.volume)
                    continue;
                out += first ? "" : ",";
                first = false;
                out += "{\"class\":" + std::to_string(key.first) + ",\"sub\":" + std::to_string(key.second);
                out += ",\"auctions\":" + std::to_string(stats.auctions);
                out += ",\"avg\":" + (stats.priced ? std::to_string(stats.priceSum / stats.priced) : std::string("null"));
                if (stats.banded)
                {
                    out += ",\"low\":" + std::to_string(stats.low / stats.banded);
                    out += ",\"usual\":" + std::to_string(stats.usual / stats.banded);
                    out += ",\"high\":" + std::to_string(stats.high / stats.banded);
                }
                else
                    out += ",\"low\":null,\"usual\":null,\"high\":null";
                out += ",\"volume_24h\":" + std::to_string(stats.volume) + "}";
            }

            out += "],\"top_sales_24h\":[";
            first = true;
            for (auto const& [item, total] : top)
            {
                out += first ? "" : ",";
                first = false;
                out += "{\"item\":" + std::to_string(item) + ",\"count\":" + std::to_string(total.first) +
                       ",\"gold\":" + std::to_string(total.second) + "}";
            }

            out += "],\"crafts_24h\":[";
            first = true;
            for (auto const& [key, total] : crafted)
            {
                out += first ? "" : ",";
                first = false;
                out += "{\"skill\":" + std::to_string(key.first) + ",\"item\":" + std::to_string(key.second) +
                       ",\"count\":" + std::to_string(total.first) + ",\"profit\":" + std::to_string(total.second) + "}";
            }

            out += "],\"orders\":[";
            first = true;
            for (auto it = orders.rbegin(); it != orders.rend(); ++it)
            {
                out += first ? "" : ",";
                first = false;
                out += "{\"id\":" + std::to_string(it->id) + ",\"buyer\":";
                Escape(out, it->buyer);
                out += ",\"bot\":";
                Escape(out, it->bot);
                out += ",\"item\":" + std::to_string(it->item) + ",\"price\":" + std::to_string(it->price) + ",\"state\":";
                Escape(out, it->state);
                out += ",\"at\":" + std::to_string(it->at) + ",\"skill\":" + std::to_string(it->skill) + ",\"kind\":";
                Escape(out, it->kind);
                out += "}";
            }

            // Smelting is the Mining crafts; disenchanting and prospecting the broken items.
            out += "],\"smelt_24h\":[";
            first = true;
            for (auto const& [key, total] : crafted)
            {
                if (key.first != SkillMining)
                    continue;
                out += first ? "" : ",";
                first = false;
                out += "{\"item\":" + std::to_string(key.second) + ",\"count\":" + std::to_string(total.first) + "}";
            }
            std::map<uint32, std::pair<uint32, uint32>> disenchanted, prospected; // item -> count, from
            for (Break const& made : breaks)
            {
                auto& total = (made.disenchant ? disenchanted : prospected)[made.item];
                total.first += made.count;
                total.second += made.from;
            }
            for (auto const& [name, list] : {std::make_pair("disenchant_24h", &disenchanted), std::make_pair("prospect_24h", &prospected)})
            {
                out += std::string("],\"") + name + "\":[";
                first = true;
                for (auto const& [item, total] : *list)
                {
                    out += first ? "" : ",";
                    first = false;
                    out += "{\"item\":" + std::to_string(item) + ",\"count\":" + std::to_string(total.first) +
                           ",\"from\":" + std::to_string(total.second) + "}";
                }
            }
            out += "]";

            // Auctions on game time: the downtime added at start-up, and what ended in the last 24 h.
            out += ",\"clock\":{\"enabled\":";
            out += clockKnown ? "1" : "0";
            out += ",\"offline_added_s\":" + (clockKnown ? std::to_string(clockOffline) : std::string("null"));
            out += ",\"resumed\":" + (clockKnown ? std::to_string(clockResumed) : std::string("null"));
            out += ",\"expired_24h\":" + std::to_string(Sum(expired));
            out += ",\"sold_24h\":" + std::to_string(Sum(sold)) + ",\"relisted_24h\":null}";

            AhBotTick total{0, 0, 0, 0, 0};
            for (AhBotTick const& tick : ahbot)
            {
                total.listed += tick.listed;
                total.bought += tick.bought;
                total.in += tick.in;
                total.out += tick.out;
            }
            out += ",\"ahbot_24h\":{\"listed\":" + std::to_string(total.listed) + ",\"bought\":" +
                   std::to_string(total.bought) + ",\"gold_in\":" + std::to_string(total.in) + ",\"gold_out\":" +
                   std::to_string(total.out) + "}";
            out += "}}";

            _lastBuildUs = uint32(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
            _writer.Post((std::filesystem::path(dir) / "economy.json").string(), std::move(out));
        }

        Writer _writer;
        QueryCallbackProcessor _gold;
        uint32 _watchTimer = 0;
        std::filesystem::file_time_type _watchStamp{};
        bool _economyWatched = false;
        uint32 _lastBuild = 0;
        uint32 _lastBuildUs = 0;
        uint32 _lastGold = 0;
        bool _goldPending = false;
        uint64 _goldAll = 0;
        uint64 _goldBots = 0;
    };

    Economy economy;

    // Every auction that ends, sold or expired, whoever listed it.
    class CoaEconomyDataAuctions : public AuctionHouseScript
    {
    public:
        CoaEconomyDataAuctions()
            : AuctionHouseScript("CoaEconomyDataAuctions", {AUCTIONHOUSEHOOK_ON_AUCTION_SUCCESSFUL, AUCTIONHOUSEHOOK_ON_AUCTION_EXPIRE}) { }

        void OnAuctionSuccessful(AuctionHouseObject* /*ah*/, AuctionEntry* /*entry*/) override { Count(sold, Now()); }
        void OnAuctionExpire(AuctionHouseObject* /*ah*/, AuctionEntry* /*entry*/) override { Count(expired, Now()); }
    };

    class CoaEconomyDataWorld : public WorldScript
    {
    public:
        CoaEconomyDataWorld()
            : WorldScript("CoaEconomyDataWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN}) { }

        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
        void OnUpdate(uint32 diff) override { economy.Update(diff); }
        void OnShutdown() override { economy.Stop(); }
    };
}

void RecordSale(uint32 itemId, uint32 count, uint64 price)
{
    if (!enabled)
        return;
    uint32 const now = Now();
    Forget(sales, now);
    sales.push_back({now, itemId, count, price});
}

void RecordCraft(uint32 skillId, uint32 itemId, uint32 count, int64 profit)
{
    if (!enabled)
        return;
    uint32 const now = Now();
    Forget(crafts, now);
    crafts.push_back({now, skillId, itemId, count, profit});
}

void RecordOrder(std::string const& buyer, std::string const& bot, uint32 itemId, uint32 price, char const* state,
                 uint32 skillId, char const* kind)
{
    if (!enabled)
        return;
    orders.push_back({nextOrderId++, Now(), buyer, bot, itemId, price, state ? state : "", skillId, kind ? kind : "player"});
    while (orders.size() > MaxOrders)
        orders.pop_front();
}

void RecordBreak(bool disenchant, uint32 itemId, uint32 count, uint32 from)
{
    if (!enabled)
        return;
    uint32 const now = Now();
    Forget(breaks, now);
    breaks.push_back({now, disenchant, itemId, count, from});
}

void RecordAhBot(uint32 listed, uint32 bought, uint64 goldIn, uint64 goldOut)
{
    if (!enabled)
        return;
    uint32 const now = Now();
    if (ahbot.empty() || ahbot.back().at / MINUTE != now / MINUTE)
    {
        Forget(ahbot, now);
        ahbot.push_back({now, 0, 0, 0, 0});
    }
    AhBotTick& tick = ahbot.back();
    tick.listed += listed;
    tick.bought += bought;
    tick.in += goldIn;
    tick.out += goldOut;
}

void RecordClock(uint32 offlineSeconds, uint32 resumed)
{
    clockKnown = true;
    clockOffline = offlineSeconds;
    clockResumed = resumed;
}

} // namespace CoaEconomy::Data

void AddSC_coa_economy_data()
{
    // Off by default; read at start-up (restart to change it). AiPlayerbot.CoaDataFiles = 0 also turns it off.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.DataFile.Enable", true, false))
        return;
    CoaEconomy::Data::LoadSettings();
    new CoaEconomy::Data::CoaEconomyDataWorld();
    new CoaEconomy::Data::CoaEconomyDataAuctions();
}
