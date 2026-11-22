/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_ECONOMY_RULES_H
#define COA_ECONOMY_RULES_H

// CoA Economy: the rules of its features, free of the core, so that tests/coa_economy_*.cpp can check them.
// The bricks call these; nothing here reads the config or the world.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
// The front the level bounds use (item and required level, profession rank, unit price cap, rare and epic steps): the
// percentile front, or the highest level of a real player's character when it is ahead. A level 80 player keeps the
// whole house on a realm of bots of all levels; a player ahead of the bots on a new realm is not held back. The stock
// keeps the percentile front.
inline std::uint32_t LevelBoundFront(std::uint32_t front, std::uint32_t playerMax)
{
    return std::max(front, playerMax);
}

// A realm under MinPopulation characters: its highest level (1 when empty).
inline std::uint32_t SmallRealmFront(std::map<std::uint32_t, std::uint32_t> const& byLevel)
{
    return byLevel.empty() ? 1 : std::max<std::uint32_t>(1, byLevel.rbegin()->first);
}

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

// The profession rank a character of this level can have: the trainer tiers (75 until level 10, 150 until 20, 225
// until 35, 300 until 50, 375 until 65, then 450), and no more than 25 plus 6 a level.
inline std::uint32_t LaunchSkillCap(std::uint32_t level)
{
    std::uint32_t const tier = level < 10 ? 75 : level < 20 ? 150 : level < 35 ? 225 : level < 50 ? 300 : level < 65 ? 375 : 450;
    return std::min<std::uint32_t>(tier, 25 + 6 * level);
}

// Beyond level and quality: goods (anything but weapons and armour) stay within 5 item levels of the cap even with a
// required level (a level 1 wine of item level 60), and nothing needs a profession rank the cap's level cannot have
// (a level 1 upgrade kit for rank 300 smiths).
inline bool LaunchAllowsGoods(LaunchCaps const& caps, bool gear, std::uint32_t itemLevel, std::uint32_t requiredSkillRank)
{
    std::uint32_t const cap = caps.front + caps.levelMargin;
    if (!gear && itemLevel > cap + 5)
        return false;
    return requiredSkillRank <= LaunchSkillCap(cap);
}

// The most one item may cost while the front is under untilLevel: goldPerLevelSquared x front^2 gold, at least 1 gold
// (front 1: 1 g, 10: 25 g, 20: 100 g, 40: 400 g at 0.25). 0 = no cap.
inline std::uint64_t LaunchUnitPriceCap(std::uint32_t front, float goldPerLevelSquared, std::uint32_t untilLevel)
{
    if (goldPerLevelSquared <= 0.0f || front >= untilLevel)
        return 0;
    return std::max<std::uint64_t>(10000, std::uint64_t(double(goldPerLevelSquared) * front * front * 10000.0));
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

// A listing price per item out of the fair range: under half or over twice the reference (the reference 0: never).
inline bool OutOfFairRange(std::uint64_t perItem, std::uint64_t reference)
{
    return reference && (perItem * 2 < reference || perItem > reference * 2);
}

// A scanned price kept between half and three times its band's median (no median: as it is; no price: 0).
// highTenths: the top of the range in tenths of the median (30 = three times; gear uses 15, see QualityStep).
inline std::uint64_t ClampToBand(std::uint64_t price, std::uint64_t median, std::uint64_t highTenths = 30,
                                 std::uint64_t lowTenths = 5)
{
    if (!price || !median)
        return price;
    return std::clamp<std::uint64_t>(price, median * lowTenths / 10, std::max(median * lowTenths / 10, median * highTenths / 10));
}

// A fair price that looks human: crafted goods sit between 1.1 and 2 times their reagents (a scan alone
// may say anything), nothing sells under what a vendor pays plus a quarter, and what a vendor sells at will never costs
// more than at the vendor. reagents 0: not crafted; vendorBuy 0: no vendor sells it.
inline std::uint64_t FairPrice(std::uint64_t value, std::uint64_t reagents, std::uint64_t vendorSell, std::uint64_t vendorBuy)
{
    if (reagents)
        value = value ? std::clamp<std::uint64_t>(value, reagents * 11 / 10, reagents * 2) : reagents * 13 / 10;
    if (value && value < vendorSell + vendorSell / 4)
        value = vendorSell + vendorSell / 4;
    if (vendorBuy && value > vendorBuy)
        value = vendorBuy;
    return value;
}

// The least price of a better quality: 1.5 times the scaled median of the quality below (0 when unknown).
// Gear's scanned price tops at 1.5 times its median, so this step puts a better quality at or over the dearest piece
// of the quality below, slot or not.
inline std::uint64_t QualityStep(std::uint64_t lowerMedian, float lowerFactor)
{
    return std::uint64_t(double(lowerMedian) * lowerFactor * 1.5);
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

// An upgrade kit (consumable, item enhancement: "Essence of Fire Upgrade Kit"). Every enchanter made it for the
// house (1,530 in bots' bags, 50 at a time at the top of it): two or three of a kit are fine, fifty are not.
inline bool IsUpgradeKit(std::uint32_t itemClass, std::uint32_t subClass)
{
    return itemClass == 0 && subClass == 6;
}

// The most auctions of one item the bots keep in one house: BotAuctions.MaxPerUpgradeKit for a kit,
// BotAuctions.MaxPerItem for anything else (0 = no cap).
inline std::uint32_t ItemCap(std::uint32_t itemClass, std::uint32_t subClass, std::uint32_t capItem,
                             std::uint32_t capKit)
{
    return IsUpgradeKit(itemClass, subClass) ? capKit : capItem;
}

// A new auction of an item only under its cap, so that one craft or one loot does not fill the house.
inline bool UnderItemCap(std::uint32_t listed, std::uint32_t cap)
{
    return cap == 0 || listed < cap;
}

// The house seller (AHBot) fills its house up to MinItems/MaxItems counting ITS auctions only: the bots' own auctions
// (about 10,000 per faction on CoA) kept it from ever listing, and with it the house's epic weapons and armour.
template <typename Owners, typename Sellers>
std::uint32_t HouseSellerCount(Owners const& owners, Sellers const& sellers)
{
    std::uint32_t count = 0;
    for (auto const owner : owners)
        if (sellers.count(owner))
            ++count;
    return count;
}

// ---- Material floor (AuctionHouse/CoaAuctionHouseBot, AHBot.MaterialFloor.*) ----

// The common crafting materials of levels 1 to 60 the house always offers: cloth, ore, bars, stone, herbs, leather and
// hides, elemental materials, enchanting materials.
inline std::vector<std::uint32_t> const& MaterialFloorItems()
{
    static std::vector<std::uint32_t> const items = {
        // Cloth
        2589, 2592, 4306, 4338, 14047,
        // Ore, bars, stone
        2770, 2771, 2775, 2772, 3858, 7911, 10620,
        2840, 2841, 2842, 3576, 3575, 3577, 3859, 3860, 6037, 12359,
        2835, 2836, 2838, 7912, 12365,
        // Herbs
        2447, 765, 2449, 785, 2450, 2452, 3820, 2453, 3369, 3355, 3356, 3357, 3818, 3821, 3358, 3819, 4625,
        8831, 8836, 8838, 8839, 8845, 8846, 13464, 13463, 13465, 13466, 13467,
        // Leather and hides
        2318, 783, 2319, 4232, 4234, 4235, 4304, 8169, 8170, 8171,
        // Elemental
        7067, 7068, 7069, 7070, 7075, 7076, 7077, 7078, 7079, 7080, 7081, 7082,
        // Enchanting
        10940, 10938, 10939, 10998, 11083, 11082, 11134, 11135, 11137, 11174, 11175, 11176, 16202, 16203, 16204,
    };
    return items;
}

// Stacks the house lists of a floor material: what is missing to reach the floor, nothing when players and bots
// already offer that many stacks, never more than the room left in the house seller's quota.
inline std::uint32_t FloorStacksToList(std::uint32_t listedStacks, std::uint32_t floorStacks, std::uint32_t quotaRoom)
{
    if (listedStacks >= floorStacks)
        return 0;
    return std::min(floorStacks - listedStacks, quotaRoom);
}

// The house's price per item of a floor material: its market price, never under the lowest price per item another
// seller asks (the floor does not undercut players).
inline std::uint64_t FloorUnitPrice(std::uint64_t marketPerItem, std::uint64_t lowestOtherPerItem)
{
    return std::max(marketPerItem, lowestOtherPerItem);
}

// 3.1.4: what the house buyer pays per item for a common crafting material another seller lists (a farmer's
// stack): pricePercent of the house's own value, never above the arbitrage cap applied after it.
inline std::uint64_t MaterialBuyerPrice(std::uint64_t houseValuePerItem, std::uint32_t pricePercent)
{
    return houseValuePerItem * pricePercent / 100;
}

// 3.1.4 guard: an item an NPC vendor sells (any stock, limited or not) is never bought above this share of the vendor
// price. With the best reputation discount (20 %) and the house's cut (5 %), buying it at a vendor to sell it to the
// house always loses: 0.95 x 75 % = 71 % < 80 %.
constexpr std::uint32_t VendorSafePercent = 75;
inline std::uint64_t VendorSafeCap(std::uint64_t perItem, std::uint64_t vendorBuyPrice)
{
    return vendorBuyPrice ? std::min(perItem, vendorBuyPrice * VendorSafePercent / 100) : perItem;
}

// 3.1.4 guard, which vendor goods it holds for: an item an NPC vendor sells in unlimited stock (maxcount 0) always;
// one sold in limited stock only when buying all the vendors' stock of a day (itemsPerDay) and selling it at payPerItem
// (the house's 5 % cut taken, the best reputation discount given) would bring more than maxCopperPerDay. Farmers are
// paid the normal price for the rest (a herbalist's Dreamfoil: 2 a day at its vendor).
constexpr std::uint64_t VendorAbuseCopperPerDay = 10 * 10000;  // 10 gold
inline bool VendorGuardApplies(bool unlimited, std::uint64_t itemsPerDay, std::uint64_t payPerItem,
                               std::uint64_t vendorBuyPrice, std::uint64_t maxCopperPerDay = VendorAbuseCopperPerDay)
{
    if (!vendorBuyPrice)
        return false;
    if (unlimited)
        return true;
    std::uint64_t const net = payPerItem * 95 / 100;
    std::uint64_t const cost = vendorBuyPrice * 80 / 100;
    return net > cost && itemsPerDay * (net - cost) > maxCopperPerDay;
}

// 3.1.4: the most the house buyer pays per item for a farmer's material: pricePercent of the house's value, under
// marginPercent of the lowest price the house asks itself (buying from the house to sell back loses), under the
// vendor guard. 0 = it does not buy.
inline std::uint64_t MaterialBuyerPay(std::uint64_t houseValuePerItem, std::uint32_t pricePercent,
                                      std::uint64_t houseLowestPerItem, std::uint32_t marginPercent,
                                      std::uint64_t vendorBuyPrice)
{
    std::uint64_t pay = MaterialBuyerPrice(houseValuePerItem, pricePercent);
    if (houseLowestPerItem)
        pay = std::min(pay, houseLowestPerItem * marginPercent / 100);
    return VendorSafeCap(pay, vendorBuyPrice);
}

// 3.1.4: a farmer's auction is bought at its own buyout, only when that buyout is within what the buyer pays for the
// stack; no bid, no buyout = no purchase. The buyer never pays more than asked, nor anything for a dearer one.
inline bool MaterialBuyerTakes(std::uint64_t buyout, std::uint32_t count, std::uint64_t payPerItem)
{
    return buyout && count && payPerItem && buyout <= payPerItem * count;
}

// 3.1.4: one account (all its characters) sells the house buyer at most perDay auctions of materials and goldCopper
// a day (0 = no cap).
inline bool AccountMaySell(std::uint32_t soldToday, std::uint64_t paidToday, std::uint64_t price, std::uint32_t perDay,
                           std::uint64_t goldCopper)
{
    if (perDay && soldToday >= perDay)
        return false;
    return !goldCopper || paidToday + price <= goldCopper;
}

// 3.1.4: which candidate the house buyer looks at next: one of the first `materials` (the crafting materials, put
// first) while any is left, else any of them. `roll` is any random number.
inline std::size_t BuyerCandidate(std::size_t materials, std::size_t total, std::uint32_t roll)
{
    if (!total)
        return 0;
    return materials ? roll % std::min(materials, total) : roll % total;
}

// 3.1.4 (player's decision, 16/11): a player's material (not a bot's) is bought at its own price when that price per
// item is at most the cheapest price the house asks for it right now (an Auctionator undercut sells); with no house
// auction of it, the usual cap (usualPay). Bots keep the usual cap: no gold for 1,300 bot accounts.
inline std::uint64_t PlayerMatchPay(bool enabled, bool playerSeller, std::uint64_t houseCheapestNow, std::uint64_t usualPay)
{
    return enabled && playerSeller && houseCheapestNow ? houseCheapestNow : usualPay;
}

// 3.1.4: the house does not buy from an account, for 24 hours, an item that account bought from the house (buy its
// cheapest stack, list it under the next one: up to 0.34 x the value an item otherwise).
constexpr std::uint32_t HouseRebuyHoldSeconds = 24 * 3600;
// Kept in memory only (emptied by a restart), at most this many (account, item) entries; the stale ones go first.
constexpr std::size_t HouseSalesMax = 20000;
inline std::uint64_t HouseSaleKey(std::uint32_t account, std::uint32_t item)
{
    return (std::uint64_t(account) << 32) | item;
}
inline bool BoughtFromHouseRecently(std::uint32_t boughtAt, std::uint32_t now, std::uint32_t hold = HouseRebuyHoldSeconds)
{
    return boughtAt && now >= boughtAt && now < boughtAt + hold;
}

// 3.1.4 (16/11, measured on a player's realm: about 8,000 gold a day paid to bots): the material buyer rewards
// players who farm; a random bot's material auction is left to players, other bots and vendors unless buyFromBots.
inline bool MaterialBuyerSkips(bool buyFromBots, bool randomBotSeller)
{
    return randomBotSeller && !buyFromBots;
}

// 3.1.4: a look at a farmer's material does not use one of the buyer's candidates of the cycle (3), only a purchase
// does, up to maxMaterialLooks looks a cycle: every seller's materials are looked at, none starves behind auctions
// refused for their price or their account's day.
constexpr std::uint32_t MaterialLooksPerCycle = 40;
inline bool BuyerLookUsesSlot(bool material, std::uint32_t materialLooks, std::uint32_t maxMaterialLooks = MaterialLooksPerCycle)
{
    return !material || materialLooks > maxMaterialLooks;
}

// The floor serves the faction houses only (Alliance 2 and Horde 6), not the neutral one (Booty Bay, Gadgetzan...).
inline bool FloorServesHouse(std::uint32_t houseId)
{
    return houseId == 2 || houseId == 6;
}

// An artisan's quote is whispered (craft_quote_addon) when a player asked it by whisper ("can you make X?": the
// whisper is the quote, and the deal waits on it); never when the addon asked, whose card shows the price: every item
// clicked in its list asked a quote, and their whispers came late, one for an item never ordered.
inline bool QuoteWhispered(bool fromAddon)
{
    return !fromAddon;
}

// A whisper without its item links ("|cff...|Hitem:...|h[Name]|h|r" and the bare "|Hitem:...|h[Name]|h"), lower
// case kept: "how much for |c..[cloak of fire]|h|r?" -> "how much for ?".
inline std::string StripItemLinks(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();)
    {
        if (text[i] == '|' && i + 1 < text.size())
        {
            char const c = char(text[i + 1] | 0x20);
            if (c == 'c' && i + 10 <= text.size())
            {
                i += 10;
                continue;
            }
            if (c == 'r')
            {
                i += 2;
                continue;
            }
            if (c == 'h')
            {
                std::size_t const name = text.find("|h[", i + 2);
                std::size_t const nameH = name == std::string::npos ? text.find("|H[", i + 2) : name;
                std::size_t const end = nameH == std::string::npos ? std::string::npos : text.find("]|", nameH + 3);
                if (end == std::string::npos)
                    break;
                i = end + 3;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// A whisper to an artisan naming an item it knows how to make asks for its quote (the craft panel's price, COD):
// the link alone ("[Cloak of Fire]", "[Cloak of Fire]?"), or a word of price, trade or craft around it ("how much
// for", "price", "wts?", "can you craft", "do you have", "combien", "prix"...). Anything else ("look at my [x]",
// "where did you get [x]") keeps its usual answer. Takes the whisper in lower case.
inline bool AsksCraftQuote(std::string const& lowered)
{
    std::string const text = StripItemLinks(lowered);
    static char const* const words[] = {
        "price", "prices", "cost", "costs", "much", "wts", "wtb", "craft", "crafts", "crafting", "make", "made",
        "sell", "selling", "buy", "buying", "have", "got", "need", "want", "lf", "lfw", "pst", "available", "quote",
        "cod", "order", "stock", "combien", "prix", "coute", "co\xc3\xbbte", "ach\xc3\xa8te", "vends", "vend", "achete", "fabriquer", "fabriques",
        "faire", "fais", "dispo", "veux", "besoin", "commande"};
    bool anyWord = false;
    std::string word;
    auto matches = [&]() {
        for (char const* w : words)
            if (word == w)
                return true;
        return false;
    };
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        char const c = i < text.size() ? text[i] : ' ';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80)
        {
            word += c;
            continue;
        }
        if (word.empty())
            continue;
        anyWord = true;
        if (matches())
            return true;
        word.clear();
    }
    return !anyWord;
}

// A line paced by a gap (seconds): allowed when never said yet, or when the gap since the last one is over. Used for
// ah_sold (one per server per AhSold.MinutesBetween) and trade_window (one per player per TradeWindow.MinutesPerPlayer).
inline bool PaceAllows(std::uint32_t lastS, std::uint32_t nowS, std::uint32_t gapS)
{
    return lastS == 0 || nowS >= lastS + gapS;
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

// 3.1.4 guild stock in the bank: how many more items of one kind go from the bags to the bot's own bank. The stock is
// `stacks` full stacks in all; an artisan keeps `keepInBags` of its reagents in the bags to craft with, the rest of
// the stock lies in the bank. What is held beyond the stock is surplus, sold at the auction house (KeepsInStock).
inline std::uint32_t BankRoom(std::uint32_t inBags, std::uint32_t inBank, std::uint32_t stackSize, std::uint32_t stacks,
                              std::uint32_t keepInBags)
{
    std::uint32_t const total = std::max<std::uint32_t>(1, stackSize) * stacks;
    std::uint32_t const keep = std::min(keepInBags, total);
    std::uint32_t const target = total - keep;
    std::uint32_t const spare = inBags > keep ? inBags - keep : 0;
    std::uint32_t const room = inBank < target ? target - inBank : 0;
    return std::min(spare, room);
}

// 3.1.4 (player's decision, 16/11): a bot artisan takes a player's lot (not a bot's nor the house's) for its stock
// when its price per item is at most the house's cheapest ask of the item right now, even over BuyBelowPercent;
// never one that account bought from the bots or the house in the last 24 hours (no loop).
inline bool ArtisanMatchesPlayer(bool enabled, bool playerSeller, std::uint64_t perItem, std::uint64_t houseCheapestNow,
                                 bool boughtFromBotsRecently)
{
    return enabled && playerSeller && houseCheapestNow && perItem <= houseCheapestNow && !boughtFromBotsRecently;
}

// The auctions of an item its house needs before a bot artisan buys one: MinListings, or PlayerMinListings for a
// player's lot matched to the house (the floor's 2 stacks and the player's one make 3).
inline std::uint32_t StockMinListings(bool playerMatch, std::uint32_t minListings, std::uint32_t playerMinListings)
{
    return playerMatch ? playerMinListings : minListings;
}

inline bool StockBargain(std::uint64_t pricePerItem, std::uint64_t marketPerItem, std::uint32_t buyBelowPercent)
{
    return marketPerItem && pricePerItem * 100 <= marketPerItem * buyBelowPercent;
}

// Guild stock (CoaEconomy.GuildStock.*): gatherers keep a little of what they gather, in their bags and their bank,
// for a guildmate's order. Trade goods (class 7) by subclass: cloth (5) for everyone, leather (6) for a skinner,
// metal and stone (7) for a miner, herbs (9) for a herbalist.
// 3.1.4: when a random bot's bags are emptied (refresh, RefreshClearsInventory), a material of its guild stock stays,
// up to stacks + 1 full stacks of it (the stock and one of surplus for the auction house); the rest is emptied as
// before. Guild stock off: nothing stays (3.1.3). keptSoFar: items of it already kept in this emptying.
inline bool RefreshKeepsStock(bool guildStock, bool isStock, std::uint32_t keptSoFar, std::uint32_t count,
                              std::uint32_t stackSize, std::uint32_t stacks)
{
    if (!guildStock || !isStock)
        return false;
    return keptSoFar + count <= std::max<std::uint32_t>(1, stackSize) * (stacks + 1);
}

inline bool GathererKeeps(std::uint32_t itemClass, std::uint32_t subClass, bool mining, bool herbalism, bool skinning)
{
    if (itemClass != 7)
        return false;
    return subClass == 5 || (subClass == 6 && skinning) || (subClass == 7 && mining) || (subClass == 9 && herbalism);
}

// The kinds a gatherer keeps: those of its level band, the closest item level to its own first (the smaller id on a
// tie, so the choice holds from one visit to the next), at most `kinds`. Candidates: (item id, item level).
// 3.1.4: the kinds a gatherer keeps are first those it holds (most held first), then the closest to its level: a
// level 60 miner holding Mithril Ore keeps Mithril, not only Thorium it has not gathered (the bench of 16/11 saw
// only Runecloth banked). candidates: (item, item level, held).
struct StockCandidate
{
    std::uint32_t item;
    std::uint32_t itemLevel;
    std::uint32_t held;
};
inline std::vector<std::uint32_t> PickHeldStockKinds(std::vector<StockCandidate> candidates, std::uint32_t level,
                                                 std::uint32_t kinds)
{
    auto distance = [level](std::uint32_t itemLevel) { return itemLevel > level ? itemLevel - level : level - itemLevel; };
    std::sort(candidates.begin(), candidates.end(), [&](StockCandidate const& a, StockCandidate const& b) {
        if ((a.held > 0) != (b.held > 0))
            return a.held > 0;
        if (a.held != b.held)
            return a.held > b.held;
        std::uint32_t const da = distance(a.itemLevel), db = distance(b.itemLevel);
        return da != db ? da < db : a.item < b.item;
    });
    std::vector<std::uint32_t> out;
    for (StockCandidate const& c : candidates)
    {
        if (out.size() >= kinds)
            break;
        out.push_back(c.item);
    }
    return out;
}

inline std::vector<std::uint32_t> PickStockKinds(std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates,
                                                 std::uint32_t level, std::uint32_t kinds)
{
    auto distance = [level](std::uint32_t itemLevel) { return itemLevel > level ? itemLevel - level : level - itemLevel; };
    std::sort(candidates.begin(), candidates.end(), [&](auto const& a, auto const& b) {
        std::uint32_t const da = distance(a.second), db = distance(b.second);
        return da != db ? da < db : a.first < b.first;
    });
    std::vector<std::uint32_t> out;
    for (auto const& c : candidates)
    {
        if (out.size() >= kinds)
            break;
        out.push_back(c.first);
    }
    return out;
}

// An intermediate an artisan keeps a stack of for its guild: a bolt of cloth (tailoring 197), a cured leather
// (leatherworking 165), a bar (mining 186: smelting), all trade goods of their subclass.
inline bool IsIntermediate(std::uint32_t skillId, std::uint32_t itemClass, std::uint32_t subClass)
{
    if (itemClass != 7)
        return false;
    return (skillId == 197 && subClass == 5) || (skillId == 165 && subClass == 6) || (skillId == 186 && subClass == 7);
}

// The guild's purchases of materials at the auction house, a day: only an item with at least minListings auctions,
// never more than maxSharePercent of them for the guild (players keep the rest), and no more than perDay purchases
// for the whole guild. listings: the item's auctions now; boughtOfItem / boughtToday: the guild's today.
// firstOfItemFree (3.1.4, a player's lot): the guild's first lot of an item a day is not held by the share (25 % of
// 3 auctions is under one lot); the next ones are.
inline bool GuildMayBuy(std::uint32_t listings, std::uint32_t boughtOfItem, std::uint32_t boughtToday,
                        std::uint32_t minListings, std::uint32_t maxSharePercent, std::uint32_t perDay,
                        bool firstOfItemFree = false)
{
    if (!perDay || boughtToday >= perDay || listings < std::max<std::uint32_t>(1, minListings))
        return false;
    if (firstOfItemFree && boughtOfItem == 0)
        return true;
    return std::uint64_t(boughtOfItem + 1) * 100 <= std::uint64_t(listings + boughtOfItem) * maxSharePercent;
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

// 3.2 (BotAuctions.Shop): a bot with no loot to sell still goes to the auction house to buy, when its visit budget
// reaches ShopThreshold, level x copperPerLevel (a green weapon for a few gold), and its last visit is
// intervalSeconds old.
inline std::uint64_t ShopThreshold(std::uint32_t level, std::uint32_t copperPerLevel)
{
    return std::uint64_t(level) * copperPerLevel;
}

inline bool WantsToShop(std::uint64_t budget, std::uint32_t level, std::uint32_t copperPerLevel)
{
    return budget > 0 && budget >= ShopThreshold(level, copperPerLevel);
}

// 3.2 (RemoteSell): a bot lists from afar only what brings more than the vendor once the house's cut is taken
// (the deposit comes back with the sale).
inline bool RemoteSellBeatsVendor(std::uint64_t buyout, std::uint32_t cutPercent, std::uint64_t vendorValue)
{
    return buyout * (100 - std::min<std::uint32_t>(cutPercent, 100)) / 100 > vendorValue;
}

// 3.2 (RemoteBuy, RemoteSell): when a bot's next turn comes. Its first one is spread over the interval by spread01
// (a hash of its guid), so a realm starting does not see every bot trade at once.
inline std::uint32_t FirstRemoteTurn(std::uint32_t now, float spread01, std::uint32_t intervalSeconds)
{
    return now + std::uint32_t(std::clamp(spread01, 0.0f, 0.999f) * float(intervalSeconds));
}

// 3.2: a wander lasts 5 minutes from its start, whatever its target: an auctioneer picked late in it was never
// reached (12 picked, 0 reached on the bench). A bot on its way to a trade NPC (an auctioneer, or a vendor it has
// business with) keeps that goal past the end of the wander, up to KEEP_TRADE_GOAL_MS after it picked it; a path
// that really fails still drops it.
constexpr std::uint32_t KEEP_TRADE_GOAL_MS = 3 * 60 * 1000;

inline bool KeepsTradeGoal(bool tradeNpc, bool reached, std::uint32_t sincePickMs)
{
    return tradeNpc && !reached && sincePickMs < KEEP_TRADE_GOAL_MS;
}

inline bool ShopDue(std::uint32_t now, std::uint32_t lastVisit, std::uint32_t intervalSeconds)
{
    return !lastVisit || now >= lastVisit + intervalSeconds;
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

// The house-wide price scale (PriceScalePercent): kept between 50 and 150 percent.
inline std::uint32_t ClampPriceScale(std::uint32_t percent)
{
    return std::clamp<std::uint32_t>(percent, 50, 150);
}

// A market value at the house-wide scale. Applied to market values only, before the vendor floor and cap (FairPrice),
// so that no scale makes buying from the house and selling to a vendor pay.
inline std::uint64_t ScalePrice(std::uint64_t value, std::uint32_t percent)
{
    return percent == 100 ? value : value / 100 * percent + value % 100 * percent / 100;
}

// A random bot's gold at its creation, upkeep and return to town, times BotStartGoldPercent (0 to 100; 0 = none).
inline std::uint32_t ScaleBotGold(std::uint32_t copper, std::uint32_t percent)
{
    percent = std::min<std::uint32_t>(percent, 100);
    return std::uint32_t(std::uint64_t(copper) * percent / 100);
}

// A random bot's gold when it is rebuilt (created, or randomized again every few hours or days): the rolled amount
// scaled. At 100 % the roll replaces its gold, as before; below, it never loses what it holds (the gold it earned
// selling and questing stays, and a bot at 0 % keeps living on it).
inline std::uint32_t RebuiltBotGold(std::uint32_t held, std::uint32_t rolled, std::uint32_t percent)
{
    std::uint32_t const scaled = ScaleBotGold(rolled, percent);
    return percent >= 100 ? scaled : std::max(held, scaled);
}

// Bots' level gold (BotLevelGold): "level:copper,level:copper" -> level -> copper. Bad entries are skipped.
inline std::map<std::uint32_t, std::uint32_t> ParseLevelGold(std::string const& text)
{
    std::map<std::uint32_t, std::uint32_t> table;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t const comma = text.find(',', start);
        std::string const entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        std::size_t const colon = entry.find(':');
        if (colon != std::string::npos)
        {
            char* end = nullptr;
            unsigned long const level = std::strtoul(entry.c_str(), &end, 10);
            char* end2 = nullptr;
            unsigned long long const copper = std::strtoull(entry.c_str() + colon + 1, &end2, 10);
            if (end == entry.c_str() + colon && level > 1 && level <= 255 && copper && copper <= 2000000000ull)
                table[std::uint32_t(level)] = std::uint32_t(copper);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return table;
}

// What a bot gets on reaching newLevel: the level's gold when it is a real level-up (one level at a time, from play),
// nothing for a bot set to a level at once (creation, randomisation): never a mass payment at start-up.
inline std::uint32_t LevelGoldFor(std::map<std::uint32_t, std::uint32_t> const& table, std::uint32_t oldLevel,
                                  std::uint32_t newLevel)
{
    if (newLevel != oldLevel + 1)
        return 0;
    auto const found = table.find(newLevel);
    return found == table.end() ? 0 : found->second;
}

// A recipe belongs to a later expansion when its product does or any of its reagents does (a classic-looking "Fel
// Iron Bar Upgrade Kit" at level 1 still needs Fel Iron Bars).
inline bool RecipeIsLaterExpansion(bool productLater, std::vector<bool> const& reagentsLater)
{
    if (productLater)
        return true;
    for (bool later : reagentsLater)
        if (later)
            return true;
    return false;
}

// An item only made by later expansion recipes is a later expansion item (none of its recipes is of the era).
inline bool OnlyFromLaterRecipes(std::vector<bool> const& recipesLater)
{
    if (recipesLater.empty())
        return false;
    for (bool later : recipesLater)
        if (!later)
            return false;
    return true;
}

// An order's missing reagents paid at delivery: an artisan short of gold buys them on credit and the cost comes out of
// the cash on delivery the player pays. Only when it lacks the gold (otherwise it pays them as before) and only when the
// price covers them. The copper owed, or 0 (paid now, or not possible).
inline std::uint64_t MaterialsOnDelivery(bool enabled, std::uint64_t missing, std::uint64_t money, std::uint64_t price)
{
    return (enabled && missing > money && missing < price) ? missing : 0;
}

// Artisans' rare recipes: whether a bot knows a recipe found in the world, drawn once per bot and recipe (the same
// artisan always keeps the same ones). percent of 100; 0 never, 100 always.
inline bool RareRecipeDrawn(std::uint64_t botGuid, std::uint32_t spellId, std::uint32_t percent)
{
    std::uint64_t x = botGuid * 0x9E3779B97F4A7C15ull ^ (std::uint64_t(spellId) << 1);
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x % 100 < percent;
}

// A profession whose recipes the bots may learn as rare ones (LearnRareRecipes): not cooking (185) nor first aid
// (129). Their recipes are bought or questbooks, and learning them in bulk set off cooking feats in town ("[X Bot]
// has earned the achievement [Lunch Lady]!", 25 cooking recipes, one after another).
inline bool RareRecipeProfession(std::uint32_t skillId)
{
    return skillId != 0 && skillId != 185 && skillId != 129;
}

// Within the bot's reach: its rank in the profession and its level are at least the recipe's and the product's.
inline bool RareRecipeReachable(std::uint32_t skillValue, std::uint32_t requiredRank, std::uint32_t botLevel,
                                std::uint32_t productLevel)
{
    return skillValue && skillValue >= requiredRank && botLevel >= productLevel;
}

} // namespace CoaEconomyRules

#endif
