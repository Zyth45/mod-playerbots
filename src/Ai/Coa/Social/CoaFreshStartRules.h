/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAFRESHSTARTRULES_H
#define PLAYERBOTS_COAFRESHSTARTRULES_H

#include <cstdint>

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
}  // namespace CoaFreshStart

#endif
