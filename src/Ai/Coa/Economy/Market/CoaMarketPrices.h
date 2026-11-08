/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_MARKET_PRICES_H
#define COA_ECONOMY_MARKET_PRICES_H

#include "Define.h"

#include <unordered_map>
#include <unordered_set>

namespace CoaEconomy
{

// What the market paid for one item, learned from real auction sales only (player or SquidBot to
// player or SquidBot, never the house bot): a usual price and a low/high band around it.
struct MarketBand
{
    uint64 Usual = 0;
    uint64 Low = 0;
    uint64 High = 0;
    uint32 Sales = 0;
    uint32 Updated = 0; // game time, seconds
};

// Market prices learned from real sales (AiPlayerbot.CoaEconomy.Market.*, off by default).
// The house bot blends its formula price toward the learned usual price, with a weight that grows
// with the number of sales and fades with time. The house's own sales and purchases never feed it,
// so the bot cannot drive its own prices up.
class MarketPrices
{
public:
    static MarketPrices* instance();

    void LoadSettings();
    void LoadFromDatabase();
    bool IsEnabled() const { return _enabled; }

    // A finished auction: learned only when neither side is a house character.
    void OnSale(uint32 itemId, uint32 itemCount, uint64 price, uint32 ownerGuidLow, uint32 buyerGuidLow);

    // The formula price moved toward the learned usual price (unchanged when nothing is learned).
    uint64 Blend(uint32 itemId, uint64 formulaPrice) const;

    MarketBand const* GetBand(uint32 itemId) const;
    float GetWeight(MarketBand const& band) const;

private:
    MarketPrices() = default;

    bool IsHouseCharacter(uint32 guidLow) const;
    void Learn(MarketBand& band, uint64 pricePerItem) const;
    void Save(uint32 itemId, MarketBand const& band) const;

    bool _enabled = false;
    bool _loaded = false;
    bool _tableExists = false;
    uint32 _minSales = 5;
    float _halfLifeDays = 3.0f;
    float _maxWeight = 0.8f;
    float _bandMin = 0.5f;
    float _bandMax = 3.0f;
    uint32 _houseAccountId = 0;
    std::unordered_set<uint32> _houseGuids;
    std::unordered_map<uint32, MarketBand> _bands;
};

} // namespace CoaEconomy

#define sCoaMarketPrices CoaEconomy::MarketPrices::instance()

#endif
