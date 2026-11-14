/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: a quest set aside for every bot when several die over and over on it (no game type, tested in
// tests/coa_world_recovery_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDRECOVERYRULES_H
#define PLAYERBOTS_COAWORLDRECOVERYRULES_H

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <utility>
#include <vector>

namespace CoaWorldRecoveryRules
{
// The death loops of one quest within SHARED_WINDOW_SECONDS: (bot, when).
constexpr time_t SHARED_WINDOW_SECONDS = 60 * 60;
using Strikes = std::vector<std::pair<uint32_t, time_t>>;

// A bot of this quest died over and over: true when `bots` different bots did within the window (then every bot
// leaves the quest aside). Older strikes are dropped; a bot counts once.
inline bool Strike(Strikes& strikes, uint32_t bot, time_t now, uint32_t bots)
{
    auto old = [now](auto const& s) { return now - s.second > SHARED_WINDOW_SECONDS || s.second > now; };
    strikes.erase(std::remove_if(strikes.begin(), strikes.end(), old), strikes.end());
    auto it = std::find_if(strikes.begin(), strikes.end(), [bot](auto const& s) { return s.first == bot; });
    if (it != strikes.end())
        it->second = now;
    else
        strikes.push_back({ bot, now });
    return bots && strikes.size() >= bots;
}
}  // namespace CoaWorldRecoveryRules

#endif
