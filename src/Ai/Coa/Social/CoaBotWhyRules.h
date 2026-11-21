/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COABOTWHYRULES_H
#define PLAYERBOTS_COABOTWHYRULES_H

#include <cstdint>
#include <string>

// CoA 3.2 (.squidbots why, AiPlayerbot.CoaWhy.Enable): what a bot is doing, for a player who meets it. Pure rules.
namespace CoaBotWhyRules
{
// The bot has moved when it is this far (yards) from where it stood still.
constexpr float MovedYards = 1.0f;

inline bool Moved(float dx, float dy, float dz) { return dx * dx + dy * dy + dz * dz > MovedYards * MovedYards; }

// "45s", "3m12s", "2h05m".
inline std::string Duration(uint64_t ms)
{
    uint64_t const s = ms / 1000;
    if (s < 60)
        return std::to_string(s) + "s";
    if (s < 3600)
        return std::to_string(s / 60) + "m" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60) + "s";
    uint64_t const m = (s / 60) % 60;
    return std::to_string(s / 3600) + "h" + (m < 10 ? "0" : "") + std::to_string(m) + "m";
}
}  // namespace CoaBotWhyRules

#endif
