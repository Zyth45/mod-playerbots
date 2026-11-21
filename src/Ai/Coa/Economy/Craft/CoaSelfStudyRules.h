/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASELFSTUDYRULES_H
#define PLAYERBOTS_COASELFSTUDYRULES_H

#include <algorithm>
#include <cstdint>

// CoA 3.2 (AiPlayerbot.CoaEconomy.SelfStudy.*, the user's go of 21/11): once an hour, a bot in a capital looks at
// what it could craft or enchant for itself, and buys the missing reagents (a vendor first, the auction house next)
// under a gold cap, only when the gain is worth it. Pure rules, no game types.
namespace CoaSelfStudyRules
{
// Gold one study may spend on reagents: a share of the bot's gold, and no more than its level allows.
inline uint64_t SpendCap(uint64_t money, uint32_t moneyPercent, uint32_t level, uint64_t copperPerLevel)
{
    return std::min<uint64_t>(money * moneyPercent / 100, uint64_t(level) * copperPerLevel);
}

// A purchase is worth it when the piece gains at least minGainPercent over what it replaces (an empty slot: any gain).
inline bool WorthBuying(float gain, float replacedScore, uint32_t minGainPercent)
{
    if (gain <= 0.0f)
        return false;
    if (replacedScore <= 0.0f)
        return true;
    return gain * 100.0f >= replacedScore * float(minGainPercent);
}

// A reagent's unit price: a vendor's when one sells it, else the auction house's; 0 = it cannot be bought.
inline uint64_t UnitPrice(uint64_t vendorPrice, uint64_t auctionPrice)
{
    return vendorPrice ? vendorPrice : auctionPrice;
}

// The first study of a bot falls somewhere in its first interval (spread01 in [0, 1)), so a start does not make every
// bot of a city study at once.
inline uint32_t FirstStudyDelay(float spread01, uint32_t intervalSeconds)
{
    float const spread = std::clamp(spread01, 0.0f, 0.999f);
    return uint32_t(spread * float(intervalSeconds));
}
}  // namespace CoaSelfStudyRules

#endif
