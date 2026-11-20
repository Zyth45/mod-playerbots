/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_REFRESH_RULES_H
#define COA_REFRESH_RULES_H

#include <cstdint>

// What a random bot's refresh (at each random teleport) does with its bags, apart from the game objects, so that
// tests/coa_refresh_rules.cpp can check it.

// Whether the refresh empties the bags first: never for a bot that keeps its bags (a self-found bot, or any bot of a
// self-found realm: it keeps what it looted and sells its surplus), otherwise as AiPlayerbot.RefreshClearsInventory
// says (0 keeps the bags, for realms where bots sell their loot at vendors).
inline bool CoaRefreshClearsInventory(bool keepsBags, bool refreshClearsInventory)
{
    return !keepsBags && refreshClearsInventory;
}

// How many items the refresh adds to bring what the bot holds up to the target: kept bags never pile up food or
// potions from one refresh to the next.
inline std::uint32_t CoaRefreshMissingCount(std::uint32_t target, std::uint32_t have)
{
    return target > have ? target - have : 0;
}

#endif
