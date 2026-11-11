/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_RULES_H
#define COA_ECONOMY_RULES_H

// CoA Economy: the rules of the session's bricks, free of the core, so that tests/coa_economy_*.cpp can check them.
// The bricks call these; nothing here reads the config or the world.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace CoaEconomyRules
{

// ---- Learned market prices (Market/CoaMarketPrices) ----

struct Band
{
    std::uint64_t usual = 0;
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    std::uint32_t sales = 0;
};

// A sale more than 4x away from a usual price backed by 3 sales or more is a mistake or a gift: not learned.
inline bool IsOutlier(Band const& band, std::uint64_t pricePerItem)
{
    return band.sales >= 3 && (pricePerItem > band.usual * 4 || pricePerItem * 4 < band.usual);
}

// One sale moves the band: the usual price is a moving average (0.8 / 0.2), an edge moves toward a sale outside it
// (0.7 / 0.3) and creeps back 3 % toward the usual price otherwise; low <= usual <= high always.
inline void Learn(Band& band, std::uint64_t price)
{
    if (band.sales == 0)
        band.usual = band.low = band.high = price;
    else
    {
        band.usual = std::uint64_t(0.8 * double(band.usual) + 0.2 * double(price));
        if (price < band.low)
            band.low = std::uint64_t(0.7 * double(band.low) + 0.3 * double(price));
        else if (band.low < band.usual)
            band.low += std::uint64_t(double(band.usual - band.low) * 0.03);
        if (price > band.high)
            band.high = std::uint64_t(0.7 * double(band.high) + 0.3 * double(price));
        else if (band.high > band.usual)
            band.high -= std::uint64_t(double(band.high - band.usual) * 0.03);
        band.low = std::min(band.low, band.usual);
        band.high = std::max(band.high, band.usual);
    }
    ++band.sales;
}

// Weight of the learned price: grows to maxWeight with minSales sales, halves every halfLifeDays without a sale.
inline float LearnedWeight(std::uint32_t sales, std::uint32_t minSales, float ageDays, float halfLifeDays, float maxWeight)
{
    float const bySales = std::min(1.0f, float(sales) / float(std::max<std::uint32_t>(1, minSales)));
    return maxWeight * bySales * std::pow(0.5f, std::max(0.0f, ageDays) / std::max(0.1f, halfLifeDays));
}

// The formula price moved toward the learned usual price, the learned price kept within [bandMin, bandMax] x formula.
// It grows with the formula price, which keeps the house buyer's no-arbitrage cap valid.
inline std::uint64_t Blend(std::uint64_t formula, std::uint64_t usual, float weight, float bandMin, float bandMax)
{
    if (!formula || !usual || weight <= 0.0f)
        return formula;
    double const learned = std::clamp(double(usual), double(formula) * bandMin, double(formula) * bandMax);
    return std::max<std::uint64_t>(1, std::uint64_t(double(formula) + (learned - double(formula)) * weight));
}

// ---- Human buyer of the house (AuctionHouse/CoaAuctionHouseBot) ----

// Chance to take an acceptable deal: base x (wanted / asked)^2, at most 1.
inline double PurchaseChance(std::uint32_t baseChancePercent, std::uint64_t wanted, std::uint64_t asked)
{
    double const ratio = asked ? double(wanted) / double(asked) : 1.0;
    return std::min(1.0, baseChancePercent / 100.0 * ratio * ratio);
}

// Delay factor before a new auction is noticed: x1 under 1 gold, x2 under 10, x4 under 100, x8 above.
inline std::uint32_t NoticeDelayFactor(std::uint64_t priceCopper)
{
    return priceCopper < 10000 ? 1 : priceCopper < 100000 ? 2 : priceCopper < 1000000 ? 4 : 8;
}

// No arbitrage: the buyer pays at most marginPercent of the lowest price the house asks for the item.
inline std::uint64_t ArbitrageCap(std::uint64_t wantedPerItem, std::uint64_t houseLowestPerItem, std::uint32_t marginPercent)
{
    return std::min(wantedPerItem, houseLowestPerItem * marginPercent / 100);
}

// ---- Rarity premium (AuctionHouse/CoaAuctionHouseBot) ----

// 1 from commonRate (drop chance, %) up, 1 + premium at rareRate or below, log scale in between.
inline double RarityFactor(double dropRate, double premium, double commonRate, double rareRate)
{
    if (dropRate <= 0.0 || commonRate <= rareRate)
        return 1.0;
    double const share = std::log(commonRate / dropRate) / std::log(commonRate / rareRate);
    return 1.0 + premium * std::clamp(share, 0.0, 1.0);
}

// ---- CoA sellable filter (AuctionHouse/CoaAuctionHouseBot) ----

// Bloodforged variants are left out while Bloodforged is off; with it on, unless allowed.
inline bool LeavesOutBloodforged(bool ruleOn, bool bloodforgedEnabled, bool allowBloodforged)
{
    return ruleOn && (!bloodforgedEnabled || !allowBloodforged);
}

// ---- Launch auction house (Launch/CoaLaunchCurve) ----

// The level that `percentile` % of the population has reached at most (levels -> characters).
inline std::uint32_t FrontLevel(std::map<std::uint32_t, std::uint32_t> const& byLevel, std::uint32_t percentile)
{
    std::uint32_t population = 0;
    for (auto const& entry : byLevel)
        population += entry.second;
    if (!population)
        return 1;
    std::uint32_t const wanted = std::max<std::uint32_t>(1, (population * percentile + 99) / 100);
    std::uint32_t seen = 0;
    for (auto const& entry : byLevel)
    {
        seen += entry.second;
        if (seen >= wanted)
            return entry.first;
    }
    return byLevel.rbegin()->first;
}

// The highest level that at least `topCount` characters have reached.
inline std::uint32_t TopLevel(std::map<std::uint32_t, std::uint32_t> const& byLevel, std::uint32_t topCount)
{
    std::uint32_t seen = 0;
    for (auto it = byLevel.rbegin(); it != byLevel.rend(); ++it)
    {
        seen += it->second;
        if (seen >= std::max<std::uint32_t>(1, topCount))
            return it->first;
    }
    return 1;
}

struct LaunchCaps
{
    std::uint32_t front = 1;
    std::uint32_t top = 1;
    std::uint32_t levelMargin = 3;
    std::uint32_t rareFromLevel = 20;
    std::uint32_t epicFromLevel = 50;
    bool allowLegendary = false;
};

// Quality: 0 grey .. 4 epic, 5+ legendary and above. An item without a required level passes up to 5 item levels
// above the cap.
inline bool LaunchAllows(LaunchCaps const& caps, std::uint32_t requiredLevel, std::uint32_t itemLevel, std::uint32_t quality)
{
    std::uint32_t const cap = caps.front + caps.levelMargin;
    if (requiredLevel > 0 ? requiredLevel > cap : itemLevel > cap + 5)
        return false;
    if (quality <= 2)
        return true;
    if (quality == 3)
        return caps.front >= caps.rareFromLevel;
    if (quality == 4)
        return caps.top >= caps.epicFromLevel && caps.top >= requiredLevel;
    return caps.allowLegendary && caps.top >= caps.epicFromLevel;
}

inline float LaunchStockFactor(float startStock, float progress)
{
    return startStock + (1.0f - startStock) * std::clamp(progress, 0.0f, 1.0f);
}

inline float LaunchPriceFactor(float premium, float progress)
{
    float const left = 1.0f - std::clamp(progress, 0.0f, 1.0f);
    return 1.0f + premium * left * left;
}

// ---- Bots that trade (Rpg/CoaRpgTrade) ----

// What goes to the vendor: vendor goods always; auction loot when bots do not use the auction house, or when the
// bags are 95 % full.
inline bool SellsAtVendor(bool vendorUsage, bool auctionUsage, bool botAuctionsOn, std::uint32_t bagUsedPercent)
{
    return vendorUsage || (auctionUsage && (!botAuctionsOn || bagUsedPercent >= 95));
}

// Ammunition is of no use on CoA: projectiles (item class 6) and quivers or ammo pouches (item class 11) are never
// listed, bought or offered, by the bots or the house.
// A listing price per item under half the reference price (the reference 0: never).
inline bool UnderHalf(std::uint64_t perItem, std::uint64_t reference)
{
    return reference && perItem * 2 < reference;
}

// A scanned price kept between half and three times its band's median (no median: as it is; no price: 0).
inline std::uint64_t ClampToBand(std::uint64_t price, std::uint64_t median)
{
    if (!price || !median)
        return price;
    return std::clamp<std::uint64_t>(price, median / 2, median * 3);
}

// The median of some prices (the lower middle one for an even count; 0 when empty).
inline std::uint64_t Median(std::vector<std::uint64_t> values)
{
    if (values.empty())
        return 0;
    std::size_t const middle = (values.size() - 1) / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    return values[middle];
}

inline bool IsAmmo(std::uint32_t itemClass)
{
    return itemClass == 6 || itemClass == 11;
}

// A Blizzard item of the later expansions (ids 21000 to 56806) asking level 55 or more, or of item level 58 or more,
// with no source in the original game's world: not on CoA (Netherweave Cloth and its bandages, Fel Iron Ore).
inline bool IsLaterExpansion(std::uint32_t itemId, std::uint32_t requiredLevel, std::uint32_t itemLevel,
                             bool originalWorldSource)
{
    return itemId >= 21000 && itemId <= 56806 && (requiredLevel >= 55 || itemLevel >= 58) && !originalWorldSource;
}

// What a bot may put at the auction house: never ammunition, a later expansion item or a vendor good (bought, not
// earned); a consumable only when its artisan made it, since a bot keeps the food, potions and bandages it uses.
inline bool BotMayList(std::uint32_t itemClass, bool crafted, bool soldByVendor, bool laterExpansion)
{
    if (IsAmmo(itemClass) || laterExpansion || soldByVendor)
        return false;
    return itemClass != 0 || crafted;
}

// Chat links the 3.3.5 client makes clickable: a spell (blue) and a trade skill recipe (gold, the shift-click link).
inline std::string SpellChatLink(std::uint32_t spellId, std::string const& name, bool recipe)
{
    return std::string(recipe ? "|cffffd000|Henchant:" : "|cff71d5ff|Hspell:") + std::to_string(spellId) + "|h[" +
           name + "]|h|r";
}

// Auctions run on game time: how long the server was off, to add to every auction's end at start-up. Nothing when
// the last sign of life is unknown or in the future, or the gap is a mere restart; at most maxSeconds.
inline std::uint32_t AuctionPause(std::uint32_t lastAlive, std::uint32_t now, std::uint32_t minSeconds,
                                  std::uint32_t maxSeconds)
{
    if (!lastAlive || now <= lastAlive)
        return 0;
    std::uint32_t const gap = now - lastAlive;
    return gap < minSeconds ? 0 : std::min(gap, maxSeconds);
}

// One way to make an item: how many one cast makes, and its reagents (item, count).
struct CraftPath
{
    std::uint32_t made = 1;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> reagents;
};

// Recursive craft price: an item costs the least of its market price and what its own reagents cost, down to depth
// levels (a Bolt of Silk Cloth: its price, or four Silk Cloth). market(item) gives 0 when unknown; pathOf(item)
// gives a CraftPath pointer, null when nothing makes it. 0 when neither is known.
template <typename Market, typename PathOf>
std::uint64_t RecursiveCost(std::uint32_t item, std::uint32_t depth, Market const& market, PathOf const& pathOf)
{
    std::uint64_t const bought = market(item);
    if (!depth)
        return bought;
    CraftPath const* path = pathOf(item);
    if (!path || path->reagents.empty())
        return bought;
    std::uint64_t made = 0;
    for (auto const& [reagent, count] : path->reagents)
    {
        std::uint64_t const each = RecursiveCost(reagent, depth - 1, market, pathOf);
        if (!each)
            return bought;
        made += each * count;
    }
    made = (made + std::max<std::uint32_t>(1, path->made) - 1) / std::max<std::uint32_t>(1, path->made);
    return bought ? std::min(bought, made) : made;
}

// Disenchanting, prospecting, smelting: worth it when what comes out is worth more than what goes in.
inline bool WorthBreaking(std::uint64_t inputValue, std::uint64_t outputValue)
{
    return outputValue > inputValue;
}

// An artisan's material stock: keeps a reagent of its recipes up to maxStacks stacks; buys one at the auction
// house only below buyBelowPercent of its market price.
inline bool KeepsInStock(std::uint32_t held, std::uint32_t stackSize, std::uint32_t maxStacks)
{
    return held <= std::max<std::uint32_t>(1, stackSize) * maxStacks;
}

inline bool StockBargain(std::uint64_t pricePerItem, std::uint64_t marketPerItem, std::uint32_t buyBelowPercent)
{
    return marketPerItem && pricePerItem * 100 <= marketPerItem * buyBelowPercent;
}

inline bool WantsVendor(bool rpgSellOn, std::uint32_t bagUsedPercent, std::uint32_t thresholdPercent, bool canSell)
{
    return rpgSellOn && bagUsedPercent >= thresholdPercent && canSell;
}

// Asking price of a bot's listing: reference x (0.85 .. 1.05 with patience) x count.
inline std::uint64_t BotListingPrice(std::uint64_t referencePerItem, float patience, std::uint32_t count)
{
    double const factor = 0.85 + 0.20 * std::clamp(patience, 0.0f, 1.0f);
    return std::max<std::uint64_t>(1, std::uint64_t(double(referencePerItem) * factor) * count);
}

// What a bot spends at most in a visit: maxPercent of its gold, down to 30 % of that for the most thrifty.
inline std::uint64_t BotBudget(std::uint64_t money, std::uint32_t maxPercent, float thrift)
{
    return std::uint64_t(double(money) * maxPercent / 100.0 * (1.0 - 0.7 * std::clamp(thrift, 0.0f, 1.0f)));
}

// ---- Artisans (Craft/CoaArtisans, Craft/CoaCraftOrders) ----

inline bool IsProfitable(std::uint64_t productValue, std::uint64_t materialCost, std::uint32_t minProfitPercent)
{
    return productValue > materialCost && productValue * 100 >= materialCost * (100 + minProfitPercent);
}

// An order's price: the piece's market value, never less than materials + fee; x 0.95 .. 1.10 with patience.
inline std::uint64_t CraftQuote(std::uint64_t productValue, std::uint64_t materialCost, std::uint32_t feePercent, float patience)
{
    std::uint64_t const floor = materialCost * (100 + feePercent) / 100;
    double const mood = 0.95 + 0.15 * std::clamp(patience, 0.0f, 1.0f);
    return std::max<std::uint64_t>(1, std::uint64_t(double(std::max(productValue, floor)) * mood));
}

} // namespace CoaEconomyRules

#endif
