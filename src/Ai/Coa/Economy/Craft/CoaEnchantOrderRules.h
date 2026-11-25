/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAENCHANTORDERRULES_H
#define PLAYERBOTS_COAENCHANTORDERRULES_H

#include <algorithm>
#include <cstdint>

// CoA 3.3.1 (AiPlayerbot.CoaEconomy.EnchantOrders.*): a bot without Enchanting, resting in a city or an inn, has an
// enchanter bot put an enchantment on one piece it wears, now and then, and pays for it in gold: the reagents at their
// market price plus the enchanter's fee. Pure rules, no game types.
namespace CoaEnchantOrderRules
{
// Item qualities, as the game numbers them.
constexpr uint32_t QualityUncommon = 2;
constexpr uint32_t QualityRare = 3;
constexpr uint32_t QualityEpic = 4;

// Who orders: a bot without Enchanting (an enchanter enchants for itself), from minLevel on.
inline bool Orders(uint32_t level, uint32_t minLevel, bool enchanter)
{
    return !enchanter && level >= minLevel;
}

// Time between two orders of a bot: the interval, halved from highLevel on (a level 60 bot orders first).
inline uint32_t IntervalSeconds(uint32_t level, uint32_t intervalHours, uint32_t highLevel)
{
    uint32_t const seconds = std::max<uint32_t>(1, intervalHours) * 3600;
    return level >= highLevel ? seconds / 2 : seconds;
}

// A piece worth enchanting, one that will not be replaced soon: an epic or a rare one; below highLevel, a green one
// too when it is close to the bot's level. 0 = not this one; otherwise the higher the better (quality first, then the
// item level). A piece that already carries a permanent enchantment is never ordered for (the caller skips it).
inline uint32_t PieceRank(uint32_t quality, uint32_t itemLevel, uint32_t botLevel, uint32_t highLevel)
{
    bool const durable = quality >= QualityRare ||
                         (quality == QualityUncommon && botLevel < highLevel && itemLevel + 5 >= botLevel);
    if (!durable)
        return 0;
    return std::min<uint32_t>(quality, QualityEpic + 1) * 1000 + std::min<uint32_t>(itemLevel, 999);
}

// The enchanter's fee: a share of the reagents' price, at least minFeePerLevel copper per level of the customer.
inline uint64_t Fee(uint64_t reagentCost, uint32_t feePercent, uint32_t level, uint64_t minFeePerLevel)
{
    return std::max<uint64_t>(reagentCost * feePercent / 100, uint64_t(level) * minFeePerLevel);
}

// What one order may cost: a share of the bot's gold (the rest stays: never ruined), and no more than its level allows.
inline uint64_t SpendCap(uint64_t money, uint32_t moneyPercent, uint32_t level, uint64_t copperPerLevel)
{
    return std::min<uint64_t>(money * std::min<uint32_t>(moneyPercent, 100) / 100, uint64_t(level) * copperPerLevel);
}

// Among the enchantments that fit the piece, the one taken: the best score the bot can pay for. A better one it cannot
// pay for does not stop a cheaper one.
inline bool Better(float score, uint64_t price, uint64_t cap, float bestScore)
{
    return score > 0.0f && price <= cap && score > bestScore;
}

// Why an order put nothing on (the hourly count).
enum class Outcome : uint32_t
{
    Done = 0,
    NoPiece,     // no worn piece worth it (or all already enchanted)
    NoEnchanter, // no enchanter bot online knows an enchantment for its pieces
    NoGold,      // an enchantment there, but over its spending cap
    Count
};
}  // namespace CoaEnchantOrderRules

#endif
