/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASESSIONRULES_H
#define PLAYERBOTS_COASESSIONRULES_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

// CoA Live sessions (AiPlayerbot.CoaLive.Sessions.Enable): random bots come and go like players. Each bot stays for
// a session of a believable length (many short ones, some long evenings), then another takes its place: the bots
// online stay the bot count set. Pure rules, tested alone.
namespace CoaSessions
{
// A session in the world, in seconds: 45 % short (15-60 min), 35 % an evening (1-2.5 h), 15 % long (2.5-5 h), 5 % a
// marathon (5-8 h). between(lo, hi) is an inclusive random draw.
inline uint32_t SessionSeconds(std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    uint32_t const roll = between(0, 99);
    if (roll < 45)
        return between(15 * 60, 60 * 60);
    if (roll < 80)
        return between(60 * 60, 150 * 60);
    if (roll < 95)
        return between(150 * 60, 300 * 60);
    return between(300 * 60, 480 * 60);
}

// Away between two sessions, in seconds: from a short break (20 min) to most of a day (8 h).
inline uint32_t OfflineSeconds(std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    return between(20 * 60, 8 * 3600);
}
}  // namespace CoaSessions

#endif
