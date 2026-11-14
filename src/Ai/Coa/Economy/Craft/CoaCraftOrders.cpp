/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCraftOrders.h"

#include "CoaEconomyConf.h"

#include "ChatHelper.h"
#include "CoaArtisans.h"
#include "CoaBotAuctions.h"
#include "CoaEconomyData.h"
#include "CoaEconomyRules.h"
#include "CoaLive.h"
#include "CoaLiveRules.h"
#include "Config.h"
#include "GameTime.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

namespace CoaEconomy::CraftOrders
{

namespace
{
    bool enabled = false;
    uint32 feePercent = 15;
    uint32 orderCooldownSeconds = 5 * MINUTE;
    uint32 tradeOfferPercent = 50;
    uint32 advertiseMs = 0; // 0 = off

    // The capitals, where artisans advertise in Trade.
    constexpr uint32 Capitals[] = {1519, 1537, 1657, 3557, 1637, 1497, 1638, 3487};

    // Per capital and faction: who spoke last and of which profession, so the next one is another.
    struct LastAdvert { ObjectGuid::LowType bot = 0; uint32 skill = 0; };
    std::map<std::pair<uint32, uint32>, LastAdvert> lastAdvert;
    uint32 advertiseTimer = 0;

    // World thread only: when each artisan took its last order.
    std::unordered_map<ObjectGuid::LowType, uint32> lastOrder;

    // World thread only: a quote awaiting the player's yes, per artisan (one at a time, 5 minutes).
    struct PendingQuote
    {
        ObjectGuid buyer;
        uint32 itemId = 0;
        uint32 price = 0;
        uint32 expires = 0;
    };
    std::unordered_map<ObjectGuid::LowType, PendingQuote> pendingQuotes;
    constexpr uint32 QuoteSeconds = 5 * MINUTE;

    void LoadSettings()
    {
        enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.CraftOrders.Enable", true, false);
        feePercent = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.FeePercent", 15);
        orderCooldownSeconds = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.OrderCooldownMinutes", 5) * MINUTE;
        tradeOfferPercent = std::min<uint32>(100, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.TradeOfferPercent", 50));
        advertiseMs = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.AdvertiseMinutes", 8) * MINUTE * IN_MILLISECONDS;
    }

    // The first item link of a (lowered) whisper: "|hitem:12345:..." -> 12345.
    uint32 LinkedItem(std::string const& lowered)
    {
        std::size_t const at = lowered.find("hitem:");
        if (at == std::string::npos)
            return 0;
        uint32 id = 0;
        for (std::size_t i = at + 6; i < lowered.size() && lowered[i] >= '0' && lowered[i] <= '9'; ++i)
            id = id * 10 + uint32(lowered[i] - '0');
        return id;
    }

    // What the artisan asks: the market value of the piece, and never less than its materials plus its fee; a
    // patient bot asks a little more, an impatient one a little less.
    uint32 Quote(Player* bot, Artisans::Recipe const& recipe)
    {
        uint64 const price = CoaEconomyRules::CraftQuote(recipe.productValue, recipe.materialCost, feePercent,
            BotAuctions::TraitsFor(bot->GetGUID()).patience);
        return uint32(std::clamp<uint64>(price, 1, MAX_MONEY_AMOUNT));
    }

    std::string FormatItem(uint32 itemId)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        return proto ? ChatHelper::FormatItem(proto) : "";
    }

    // The order is agreed: crafts the piece and mails it cash on delivery at the price said (busy when it cannot).
    bool Deliver(Player* from, Player* bot, Artisans::Recipe const& recipe, uint32 price,
                 std::unordered_map<std::string, std::string>& slots, bool quoteFirst)
    {
        Item* crafted = Artisans::Craft(bot, recipe);
        if (!crafted)
        {
            Data::RecordOrder(from->GetName(), bot->GetName(), recipe.itemId, price, "busy", recipe.skillId);
            return CoaLive::SayLine(bot, "wh_craft_busy", slots, from->GetGUID()), true;
        }
        lastOrder[bot->GetGUID().GetCounter()] = uint32(GameTime::GetGameTime().count());

        slots["price"] = ChatHelper::formatMoney(price);
        slots["profession"] = Artisans::ProfessionLink(bot, recipe.skillId);
        slots["recipe"] = Artisans::RecipeLink(bot, recipe);
        if (quoteFirst)
            CoaLive::SayLine(bot, "wh_craft_quote", slots, from->GetGUID());
        if (CoaLive::MailCod(bot, from, crafted->GetGUID(), price))
        {
            Data::RecordOrder(from->GetName(), bot->GetName(), recipe.itemId, price, "mailed", recipe.skillId);
            CoaLive::SayLine(bot, "wh_craft_cod", slots, from->GetGUID());
        }
        return true;
    }

    // "can you make [item]?" to a bot (CoA Live's "craft" intent, world thread).
    bool OnCraftWhisper(Player* from, Player* bot, std::string const& lowered, std::string const& /*intent*/)
    {
        if (!enabled || !Artisans::IsEnabled() || !from || !bot)
            return false;

        uint32 const itemId = LinkedItem(lowered);
        std::unordered_map<std::string, std::string> slots;
        if (itemId)
            slots["item"] = FormatItem(itemId);

        Artisans::Recipe recipe;
        bool known = false;
        if (itemId)
            for (Artisans::Recipe const& candidate : Artisans::KnownRecipes(bot))
                if (candidate.itemId == itemId)
                {
                    recipe = candidate;
                    known = true;
                    break;
                }

        // Not a recipe it knows, or a buyer of the other faction (the mail cannot cross).
        if (!known || from->GetTeamId() != bot->GetTeamId())
        {
            Data::RecordOrder(from->GetName(), bot->GetName(), itemId, 0, "refused");
            return CoaLive::SayLine(bot, "wh_craft_cannot", slots, from->GetGUID()), true;
        }

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32& last = lastOrder[bot->GetGUID().GetCounter()];
        if (last && now < last + orderCooldownSeconds)
        {
            Data::RecordOrder(from->GetName(), bot->GetName(), itemId, 0, "busy", recipe.skillId);
            return CoaLive::SayLine(bot, "wh_craft_busy", slots, from->GetGUID()), true;
        }

        uint32 const price = Quote(bot, recipe);
        // Ship only on a clear yes ("make me [item], send it COD"); a question, the price included, gets a quote and
        // the artisan waits for the answer.
        if (CoaLive::SaysSendIt(CoaLive::NormalizeWhisper(lowered)))
            return Deliver(from, bot, recipe, price, slots, true);

        slots["price"] = ChatHelper::formatMoney(price);
        slots["qty"] = "1";
        slots["profession"] = Artisans::ProfessionLink(bot, recipe.skillId);
        if (!CoaEconomyRules::QuoteWhispered(false) || !CoaLive::SayLine(bot, "craft_quote_addon", slots, from->GetGUID()))
            return false;
        pendingQuotes[bot->GetGUID().GetCounter()] = PendingQuote{from->GetGUID(), itemId, price, now + QuoteSeconds};
        return true;
    }

    // Any whisper to an artisan that quoted this player: a clear yes crafts and mails the piece at the price quoted,
    // a no or another item forgets the quote; anything else gets the usual answer and the quote stays.
    bool OnQuoteAnswer(Player* from, Player* bot, std::string const& lowered, std::string const& /*intent*/)
    {
        if (!enabled || !from || !bot || pendingQuotes.empty())
            return false;
        auto it = pendingQuotes.find(bot->GetGUID().GetCounter());
        if (it == pendingQuotes.end() || it->second.buyer != from->GetGUID())
            return false;
        PendingQuote const quote = it->second;
        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const linked = LinkedItem(lowered);
        std::string const normalized = CoaLive::NormalizeWhisper(lowered);
        if (now >= quote.expires || (linked && linked != quote.itemId) || CoaLive::DeclinesDeal(normalized))
        {
            pendingQuotes.erase(it);
            return false;
        }
        if (!CoaLive::ConfirmsDeal(normalized, lowered, quote.itemId))
            return false;
        pendingQuotes.erase(it);

        std::unordered_map<std::string, std::string> slots;
        slots["item"] = FormatItem(quote.itemId);
        for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
            if (recipe.itemId == quote.itemId)
                return Deliver(from, bot, recipe, quote.price, slots, false);
        Data::RecordOrder(from->GetName(), bot->GetName(), quote.itemId, 0, "refused");
        return CoaLive::SayLine(bot, "wh_craft_cannot", slots, from->GetGUID()), true;
    }

    bool IsCapital(uint32 zoneId)
    {
        return std::find(std::begin(Capitals), std::end(Capitals), zoneId) != std::end(Capitals);
    }

    // In each capital where a real player is, one artisan of that player's faction says it takes orders (Trade,
    // trade_craft_lfw, with its profession and recipe links): another artisan and another profession than last time.
    void Advertise()
    {
        std::vector<std::pair<uint32, uint32>> heard; // zone, team
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (player && player->IsInWorld() && IsRealPlayer(player) && IsCapital(player->GetZoneId()))
            {
                std::pair<uint32, uint32> const place{player->GetZoneId(), uint32(player->GetTeamId())};
                if (std::find(heard.begin(), heard.end(), place) == heard.end())
                    heard.push_back(place);
            }
        if (heard.empty())
            return;

        std::map<std::pair<uint32, uint32>, std::vector<Player*>> artisans;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
            if (bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsInCombat())
            {
                std::pair<uint32, uint32> const place{bot->GetZoneId(), uint32(bot->GetTeamId())};
                if (std::find(heard.begin(), heard.end(), place) != heard.end())
                    artisans[place].push_back(bot);
            }

        for (auto const& place : heard)
        {
            std::vector<Player*>& bots = artisans[place];
            if (bots.empty())
                continue;
            LastAdvert& last = lastAdvert[place];
            Acore::Containers::RandomShuffle(bots);
            // The first bot (not the last speaker) with a recipe of another profession; failing that, any recipe.
            Player* speaker = nullptr;
            Artisans::Recipe chosen;
            uint32 tried = 0;
            for (Player* bot : bots)
            {
                if (++tried > 12)
                    break;
                if (bot->GetGUID().GetCounter() == last.bot && bots.size() > 1)
                    continue;
                std::vector<Artisans::Recipe> recipes = Artisans::KnownRecipes(bot);
                if (recipes.empty())
                    continue;
                Acore::Containers::RandomShuffle(recipes);
                auto other = std::find_if(recipes.begin(), recipes.end(),
                                          [&](Artisans::Recipe const& r) { return r.skillId != last.skill; });
                if (other != recipes.end())
                {
                    speaker = bot;
                    chosen = *other;
                    break;
                }
                if (!speaker)
                {
                    speaker = bot;
                    chosen = recipes.front();
                }
            }
            if (!speaker)
                continue;

            std::unordered_map<std::string, std::string> slots;
            slots["item"] = FormatItem(chosen.itemId);
            slots["profession"] = Artisans::ProfessionLink(speaker, chosen.skillId);
            slots["recipe"] = Artisans::RecipeLink(speaker, chosen);
            if (CoaLive::SayLine(speaker, "trade_craft_lfw", slots))
                last = {speaker->GetGUID().GetCounter(), chosen.skillId};
        }
    }

    class CoaCraftOrdersWorld : public WorldScript
    {
    public:
        CoaCraftOrdersWorld() : WorldScript("CoaCraftOrdersWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }

        void OnUpdate(uint32 diff) override
        {
            if (!advertiseMs || !Artisans::IsEnabled())
                return;
            advertiseTimer += diff;
            if (advertiseTimer < advertiseMs)
                return;
            advertiseTimer = 0;
            Advertise();
        }
    };
}

bool IsEnabled()
{
    return enabled;
}

uint32 QuoteFor(Player* bot, Artisans::Recipe const& recipe)
{
    return bot ? Quote(bot, recipe) : 0;
}

bool IsBusy(Player* bot)
{
    if (!bot)
        return true;
    auto found = lastOrder.find(bot->GetGUID().GetCounter());
    uint32 const now = uint32(GameTime::GetGameTime().count());
    return found != lastOrder.end() && found->second && now < found->second + orderCooldownSeconds;
}

void MarkBusy(Player* bot)
{
    if (bot)
        lastOrder[bot->GetGUID().GetCounter()] = uint32(GameTime::GetGameTime().count());
}

bool OfferInTrade(Player* bot, Item* crafted, unsigned long long price)
{
    if (!enabled || !bot || !crafted || urand(1, 100) > tradeOfferPercent)
        return false;

    uint32 const itemId = crafted->GetEntry();
    std::unordered_map<std::string, std::string> slots;
    slots["item"] = FormatItem(itemId);
    slots["price"] = ChatHelper::formatMoney(uint32(std::min<unsigned long long>(price, MAX_MONEY_AMOUNT)));
    for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
        if (recipe.itemId == itemId)
        {
            slots["profession"] = Artisans::ProfessionLink(bot, recipe.skillId);
            slots["recipe"] = Artisans::RecipeLink(bot, recipe);
            break;
        }

    if (CoaLive::SayOffer(bot, "trade_craft", crafted->GetGUID(), uint32(std::min<unsigned long long>(price, MAX_MONEY_AMOUNT)), slots))
        return true;

    // No offer this time (rules, chance): maybe a word that it takes orders; the piece goes to the auction house.
    CoaLive::SayLine(bot, "trade_craft_lfw", slots);
    return false;
}

} // namespace CoaEconomy::CraftOrders

void AddSC_coa_economy_craft_orders()
{
    // Off by default; read at start-up (restart to change it). The settings below it are read again on reload.
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.CraftOrders.Enable", true, false))
        return;
    CoaEconomy::CraftOrders::LoadSettings();
    CoaLive::RegisterWhisperHandler("craft", &CoaEconomy::CraftOrders::OnCraftWhisper);
    CoaLive::RegisterWhisperHandler("*", &CoaEconomy::CraftOrders::OnQuoteAnswer);
    new CoaEconomy::CraftOrders::CoaCraftOrdersWorld();
}
