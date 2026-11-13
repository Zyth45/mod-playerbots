/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEconomyChatter.h"

#include "AuctionHouseMgr.h"
#include "ChatHelper.h"
#include "CoaEconomyConf.h"
#include "CoaEconomyRules.h"
#include "CoaLive.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace CoaEconomy::Chatter
{
namespace
{
    struct Settings
    {
        bool ahSold = true;
        uint32 ahSoldGapS = 30 * MINUTE;
        bool tradeWindow = true;
        uint32 tradeWindowGapS = 5 * MINUTE;
    };
    Settings settings;
    std::mutex paceLock;
    uint32 lastAhSold = 0;
    std::unordered_map<ObjectGuid::LowType, uint32> lastTradeWindow; // by player

    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }

    void Load()
    {
        settings.ahSold = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Chatter.AhSold.Enable", true);
        settings.ahSoldGapS = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Chatter.AhSold.MinutesBetween", 30) * MINUTE;
        settings.tradeWindow = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Chatter.TradeWindow.Enable", true);
        settings.tradeWindowGapS =
            CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Chatter.TradeWindow.MinutesPerPlayer", 5) * MINUTE;
    }

    // A bot's auction sold (the core's hook, the world thread): at most one line per server per gap, said in Trade by
    // the seller when it is a random bot in the world. The gap starts only when the line is said.
    void OnSold(AuctionEntry const* entry)
    {
        if (!settings.ahSold || !entry || !CoaLive::ChatOn())
            return;
        uint32 const now = Now();
        {
            std::lock_guard<std::mutex> lock(paceLock);
            if (!CoaEconomyRules::PaceAllows(lastAhSold, now, settings.ahSoldGapS))
                return;
        }
        Player* seller = ObjectAccessor::FindConnectedPlayer(entry->owner);
        if (!seller || !seller->IsInWorld() || !sRandomPlayerbotMgr.IsRandomBot(seller))
            return;
        std::unordered_map<std::string, std::string> slots;
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry->item_template))
            slots["item"] = ChatHelper::FormatItem(proto, entry->itemCount > 1 ? entry->itemCount : 0);
        slots["price"] = ChatHelper::formatMoney(entry->buyout ? entry->buyout : entry->bid);
        if (!CoaLive::SayLine(seller, "ah_sold", slots))
            return;
        std::lock_guard<std::mutex> lock(paceLock);
        lastAhSold = now;
    }

    class CoaEconomyChatterAuctions : public AuctionHouseScript
    {
    public:
        CoaEconomyChatterAuctions() : AuctionHouseScript("CoaEconomyChatterAuctions", {AUCTIONHOUSEHOOK_ON_AUCTION_SUCCESSFUL}) { }
        void OnAuctionSuccessful(AuctionHouseObject* /*ah*/, AuctionEntry* entry) override { OnSold(entry); }
    };

    class CoaEconomyChatterWorld : public WorldScript
    {
    public:
        CoaEconomyChatterWorld() : WorldScript("CoaEconomyChatterWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { Load(); }
    };
}

bool OnTradeRefused(Player* bot, Player* trader)
{
    if (!settings.tradeWindow || !bot || !trader || !CoaLive::ChatOn() || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    uint32 const now = Now();
    ObjectGuid::LowType const key = trader->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(paceLock);
        auto const found = lastTradeWindow.find(key);
        // Within the gap the bot says nothing at all: a player who opens the window ten times gets one answer.
        if (found != lastTradeWindow.end() && !CoaEconomyRules::PaceAllows(found->second, now, settings.tradeWindowGapS))
            return true;
    }
    std::unordered_map<std::string, std::string> slots;
    slots["player"] = trader->GetName();
    if (!CoaLive::SayLine(bot, "trade_window", slots, trader->GetGUID(), true))
        return false;
    std::lock_guard<std::mutex> lock(paceLock);
    lastTradeWindow[key] = now;
    if (lastTradeWindow.size() > 4096)
        lastTradeWindow.clear();
    return true;
}
} // namespace CoaEconomy::Chatter

void AddSC_coa_economy_chatter()
{
    CoaEconomy::Chatter::Load();
    new CoaEconomy::Chatter::CoaEconomyChatterAuctions();
    new CoaEconomy::Chatter::CoaEconomyChatterWorld();
}
