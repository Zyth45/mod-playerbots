/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEconomyDb.h"
#include "CoaMarketPrices.h"

#include "AccountMgr.h"
#include "AuctionHouseMgr.h"
#include "CharacterCache.h"
#include "CoaAuctionHouseBot.h"
#include "CoaEconomyData.h"
#include "CoaEconomyRules.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"

#include <algorithm>
#include <cmath>

namespace CoaEconomy
{

namespace
{
    // Account of the house bot's automatic character (see CoaAuctionHouseBot.cpp).
    constexpr char const* HouseAccountName = "AHBOT";
    constexpr char const* TableName = "coa_economy_market";

    // How a sale moves the band and when it is ignored: CoaEconomyRules::Learn / IsOutlier.
}

MarketPrices* MarketPrices::instance()
{
    static MarketPrices instance;
    return &instance;
}

void MarketPrices::LoadSettings()
{
    _enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Market.Enable", false, false);
    _minSales = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaEconomy.Market.MinSales", 5));
    _halfLifeDays = std::max(0.1f, sConfigMgr->GetOption<float>("AiPlayerbot.CoaEconomy.Market.HalfLifeDays", 3.0f));
    _maxWeight = std::clamp(sConfigMgr->GetOption<float>("AiPlayerbot.CoaEconomy.Market.MaxWeightPercent", 80.0f) / 100.0f, 0.0f, 1.0f);
    _bandMin = std::max(0.01f, sConfigMgr->GetOption<float>("AiPlayerbot.CoaEconomy.Market.BandMinPercent", 50.0f) / 100.0f);
    _bandMax = std::max(_bandMin, sConfigMgr->GetOption<float>("AiPlayerbot.CoaEconomy.Market.BandMaxPercent", 300.0f) / 100.0f);

    _houseGuids.clear();
    std::string const guids = sConfigMgr->GetOption<std::string>("AuctionHouseBot.GUIDs", "0");
    for (std::string_view token : Acore::Tokenize(guids, ',', false))
        if (Optional<uint32> guid = Acore::StringTo<uint32>(token); guid && *guid)
            _houseGuids.insert(*guid);
}

void MarketPrices::LoadFromDatabase()
{
    if (!_enabled || _loaded)
        return;
    _loaded = true;

    _houseAccountId = AccountMgr::GetId(HouseAccountName);

    _tableExists = Db::HasTable(TableName, {"item", "usual", "low", "high", "sales", "updated"});
    if (!_tableExists)
    {
        LOG_ERROR("server.loading", "CoA Economy: table {} is missing or out of date in the playerbots database; market prices are learned "
            "for this session only", TableName);
        return;
    }

    QueryResult result = PlayerbotsDatabase.Query("SELECT item, usual, low, high, sales, updated FROM {}", TableName);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            MarketBand& band = _bands[fields[0].Get<uint32>()];
            band.Usual = fields[1].Get<uint64>();
            band.Low = fields[2].Get<uint64>();
            band.High = fields[3].Get<uint64>();
            band.Sales = fields[4].Get<uint32>();
            band.Updated = fields[5].Get<uint32>();
        } while (result->NextRow());
    }
    LOG_INFO("server.loading", "CoA Economy: {} market prices learned from real sales", _bands.size());
}

bool MarketPrices::IsHouseCharacter(uint32 guidLow) const
{
    if (!guidLow)
        return false;
    if (_houseGuids.count(guidLow))
        return true;
    for (auto const& character : auctionbot->AHCharacters)
        if (character.CharacterGUID == guidLow)
            return true;
    return _houseAccountId &&
        sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid::Create<HighGuid::Player>(guidLow)) == _houseAccountId;
}

void MarketPrices::Learn(MarketBand& band, uint64 price) const
{
    CoaEconomyRules::Band rule{band.Usual, band.Low, band.High, band.Sales};
    CoaEconomyRules::Learn(rule, price);
    band.Usual = rule.usual;
    band.Low = rule.low;
    band.High = rule.high;
    band.Sales = rule.sales;
}

void MarketPrices::OnSale(uint32 itemId, uint32 itemCount, uint64 price, uint32 ownerGuidLow, uint32 buyerGuidLow)
{
    if (!_enabled || !itemId || !price || !itemCount)
        return;
    if (IsHouseCharacter(ownerGuidLow) || IsHouseCharacter(buyerGuidLow))
        return;

    Data::RecordSale(itemId, itemCount, price);

    uint64 const perItem = std::max<uint64>(1, price / itemCount);
    MarketBand& band = _bands[itemId];
    if (CoaEconomyRules::IsOutlier({band.Usual, band.Low, band.High, band.Sales}, perItem))
        return;

    Learn(band, perItem);
    band.Updated = uint32(GameTime::GetGameTime().count());
    Save(itemId, band);
}

void MarketPrices::Save(uint32 itemId, MarketBand const& band) const
{
    if (!_tableExists)
        return;
    PlayerbotsDatabase.Execute("REPLACE INTO {} (item, usual, low, high, sales, updated) VALUES ({}, {}, {}, {}, {}, {})",
        TableName, itemId, band.Usual, band.Low, band.High, band.Sales, band.Updated);
}

MarketBand const* MarketPrices::GetBand(uint32 itemId) const
{
    auto found = _bands.find(itemId);
    return found != _bands.end() && found->second.Sales ? &found->second : nullptr;
}

float MarketPrices::GetWeight(MarketBand const& band) const
{
    uint32 const now = uint32(GameTime::GetGameTime().count());
    float const ageDays = now > band.Updated ? float(now - band.Updated) / float(DAY) : 0.0f;
    return CoaEconomyRules::LearnedWeight(band.Sales, _minSales, ageDays, _halfLifeDays, _maxWeight);
}

uint64 MarketPrices::Blend(uint32 itemId, uint64 formulaPrice) const
{
    if (!_enabled || !formulaPrice)
        return formulaPrice;
    MarketBand const* band = GetBand(itemId);
    if (!band)
        return formulaPrice;

    // The learned price stays within a band around the formula, so a thin market cannot run away.
    return CoaEconomyRules::Blend(formulaPrice, band->Usual, GetWeight(*band), _bandMin, _bandMax);
}

} // namespace CoaEconomy

namespace
{
    class CoaMarketPricesWorld : public WorldScript
    {
    public:
        CoaMarketPricesWorld() : WorldScript("CoaMarketPricesWorld", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP}) { }

        void OnAfterConfigLoad(bool /*reload*/) override { sCoaMarketPrices->LoadSettings(); }
        void OnStartup() override { sCoaMarketPrices->LoadFromDatabase(); }
    };

    class CoaMarketPricesAuctions : public AuctionHouseScript
    {
    public:
        CoaMarketPricesAuctions() : AuctionHouseScript("CoaMarketPricesAuctions", {AUCTIONHOUSEHOOK_ON_AUCTION_SUCCESSFUL}) { }

        // Fired for a buyout and for a won bid; the final price is in bid.
        void OnAuctionSuccessful(AuctionHouseObject* /*ah*/, AuctionEntry* auction) override
        {
            if (!auction)
                return;
            sCoaMarketPrices->OnSale(auction->item_template, auction->itemCount, auction->bid,
                auction->owner.GetCounter(), auction->bidder.GetCounter());
        }
    };
}

void AddSC_coa_economy_market()
{
    // Off by default; read at start-up (restart to change it).
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Market.Enable", false, false))
        return;

    sCoaMarketPrices->LoadSettings();
    new CoaMarketPricesWorld();
    new CoaMarketPricesAuctions();
}
