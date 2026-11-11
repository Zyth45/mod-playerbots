/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAFRESHSTARTRULES_H
#define PLAYERBOTS_COAFRESHSTARTRULES_H

#include <algorithm>
#include <cstdint>
#include <vector>

// The launch of a new realm (AiPlayerbot.CoaFreshStart.Enable, "New living server" of the launcher, with the Solo
// self-found preset): every random bot starts at the level it was made at (1), none is rolled to 60 on day one, and
// each bot keeps a pace of its own, so the population spreads out like players at a launch: a few fast ones, the
// mass, the late ones. Pure rules.
namespace CoaFreshStart
{
enum class Pace : uint8_t
{
    Fast,  // 15 %: the racers, first 60s
    Mass,  // 65 %
    Late,  // 20 %: the slow ones, there for weeks
};

// Always the same pace for the same bot (its guid, mixed so that neighbouring guids differ).
inline Pace PaceOf(uint64_t guid)
{
    uint64_t z = guid + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    uint32_t const roll = static_cast<uint32_t>(z % 100);
    return roll < 15 ? Pace::Fast : roll < 80 ? Pace::Mass : Pace::Late;
}

// Experience of the bot, in percent of what it earned.
inline uint32_t XpPercent(Pace p)
{
    switch (p)
    {
        case Pace::Fast: return 150;
        case Pace::Late: return 60;
        default: return 100;
    }
}

inline uint32_t ScaleXp(uint32_t amount, uint64_t guid)
{
    return static_cast<uint32_t>(uint64_t(amount) * XpPercent(PaceOf(guid)) / 100);
}

// The catch-up (AiPlayerbot.CoaFreshStart.CatchUp.*): a random bot playing alone or with bots, behind the human
// players of the realm, earns more experience the further behind it is, so the bot world keeps up with the players
// without one rushing player pulling it. Never less than its pace: a bot that loses experience looks broken.
struct CatchUpSettings
{
    uint32_t marginLevels = 3;       // no bonus within this many levels under the front
    uint32_t stepPercent = 25;       // +25 % for each level behind beyond the margin
    uint32_t maxRatePercent = 400;   // the bonus never above x4
    uint32_t minHumans = 1;          // fewer active humans: no front, no bonus (1: a player alone counts)
    uint32_t maxTotalPercent = 600;  // pace x bonus x RandomBotXPRate never above x6 (the bonus gives way first)
};

// The front of the human players: the median of the highest character of each active human account (the lower
// middle when even), 0 with fewer than minHumans accounts.
inline uint32_t MedianFront(std::vector<uint32_t> levels, uint32_t minHumans)
{
    if (levels.empty() || levels.size() < std::max<uint32_t>(minHumans, 1))
        return 0;
    std::sort(levels.begin(), levels.end());
    return levels[(levels.size() - 1) / 2];
}

// The bonus of a bot at this level, in percent (100: none).
inline uint32_t CatchUpPercent(uint32_t botLevel, uint32_t front, CatchUpSettings const& s)
{
    if (!front || uint64_t(botLevel) + s.marginLevels >= front)
        return 100;
    uint64_t const behind = uint64_t(front) - botLevel - s.marginLevels;
    uint64_t const pct = 100 + uint64_t(s.stepPercent) * behind;
    return static_cast<uint32_t>(std::min<uint64_t>(pct, std::max<uint32_t>(s.maxRatePercent, 100)));
}

// What the bot earns, in percent, before the random-bot rate the core applies too (baseRatePercent, 100: x1): its
// pace, times the catch-up when on. The total is kept under maxTotalPercent by the bonus only, never under the pace.
inline uint32_t FreshStartPercent(uint64_t guid, uint32_t botLevel, uint32_t front, bool catchUp,
                                  CatchUpSettings const& s, uint32_t baseRatePercent)
{
    uint64_t const pace = XpPercent(PaceOf(guid));
    if (!catchUp)
        return static_cast<uint32_t>(pace);
    uint64_t pct = pace * CatchUpPercent(botLevel, front, s) / 100;
    if (s.maxTotalPercent && baseRatePercent)
        pct = std::min<uint64_t>(pct, uint64_t(s.maxTotalPercent) * 100 / baseRatePercent);
    return static_cast<uint32_t>(std::max<uint64_t>(pct, pace));
}

// The level a share of the bots has reached at most (a histogram by level), for the hourly record.
inline uint32_t LevelAtPercentile(std::vector<uint32_t> const& countByLevel, uint32_t percent)
{
    uint64_t total = 0;
    for (uint32_t c : countByLevel)
        total += c;
    if (!total)
        return 0;
    uint64_t const wanted = std::max<uint64_t>(1, (total * percent + 99) / 100);
    uint64_t seen = 0;
    for (size_t level = 0; level < countByLevel.size(); ++level)
        if ((seen += countByLevel[level]) >= wanted)
            return static_cast<uint32_t>(level);
    return static_cast<uint32_t>(countByLevel.size() - 1);
}
}  // namespace CoaFreshStart

#endif
