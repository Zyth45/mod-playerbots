/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaCraftOrders.h"

#include "ChatHelper.h"
#include "CoaArtisans.h"
#include "CoaBotAuctions.h"
#include "CoaEconomyData.h"
#include "CoaEconomyRules.h"
#include "CoaLive.h"
#include "Config.h"
#include "GameTime.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <unordered_map>

namespace CoaEconomy::CraftOrders
{

namespace
{
    bool enabled = false;
    uint32 feePercent = 15;
    uint32 orderCooldownSeconds = 5 * MINUTE;
    uint32 tradeOfferPercent = 50;

    // World thread only: when each artisan took its last order.
    std::unordered_map<ObjectGuid::LowType, uint32> lastOrder;

    void LoadSettings()
    {
        enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.CraftOrders.Enable", false, false);
        feePercent = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.FeePercent", 15);
        orderCooldownSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.OrderCooldownMinutes", 5) * MINUTE;
        tradeOfferPercent = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.CraftOrders.TradeOfferPercent", 50));
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
            Data::RecordOrder(from->GetName(), bot->GetName(), itemId, 0, "busy");
            return CoaLive::SayLine(bot, "wh_craft_busy", slots, from->GetGUID()), true;
        }

        uint32 const price = Quote(bot, recipe);
        Item* crafted = Artisans::Craft(bot, recipe);
        if (!crafted)
        {
            Data::RecordOrder(from->GetName(), bot->GetName(), itemId, price, "busy");
            return CoaLive::SayLine(bot, "wh_craft_busy", slots, from->GetGUID()), true;
        }
        last = now;

        slots["price"] = ChatHelper::formatMoney(price);
        slots["profession"] = Artisans::ProfessionLink(bot, recipe.skillId);
        slots["recipe"] = Artisans::RecipeLink(bot, recipe);
        CoaLive::SayLine(bot, "wh_craft_quote", slots, from->GetGUID());
        if (CoaLive::MailCod(bot, from, crafted->GetGUID(), price))
        {
            Data::RecordOrder(from->GetName(), bot->GetName(), itemId, price, "mailed");
            CoaLive::SayLine(bot, "wh_craft_cod", slots, from->GetGUID());
        }
        return true;
    }

    class CoaCraftOrdersWorld : public WorldScript
    {
    public:
        CoaCraftOrdersWorld() : WorldScript("CoaCraftOrdersWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) { }
        void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
    };
}

bool IsEnabled()
{
    return enabled;
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
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.CraftOrders.Enable", false, false))
        return;
    CoaEconomy::CraftOrders::LoadSettings();
    CoaLive::RegisterWhisperHandler("craft", &CoaEconomy::CraftOrders::OnCraftWhisper);
    new CoaEconomy::CraftOrders::CoaCraftOrdersWorld();
}
