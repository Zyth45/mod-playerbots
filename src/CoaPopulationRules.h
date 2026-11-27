/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_POPULATION_RULES_H
#define COA_POPULATION_RULES_H

#include <cmath>
#include <cstdint>

// The random bots' population, apart from the game objects, so that tests/coa_population_rules.cpp can check it.

// 3.2.3 (user, 24/11: "1000 = 1500"): AiPlayerbot.PeriodicOnlineOfflineRatio's default, 2.0 before.
constexpr float COA_PERIODIC_RATIO_DEFAULT = 1.5f;

// The random bots a realm keeps (online and offline) for MaxRandomBots online: maxBots, times the ratio with the
// periodic online-offline on. Rounded up. A ratio under 1 counts as 1.
inline std::uint32_t CoaPeriodicBotTotal(std::uint32_t maxBots, bool periodic, float ratio)
{
    if (!periodic)
        return maxBots;
    return std::uint32_t(std::ceil(double(maxBots) * (ratio < 1.0f ? 1.0 : double(ratio)) - 1e-6));
}

// Of `accounts` random bot accounts, those used at once with the periodic online-offline on: accounts / ratio,
// rounded up (the old (n + r - 1) / r on a float ratio rounded 5 / 1.5 down to 3).
inline std::uint32_t CoaPeriodicAccountsToUse(std::uint32_t accounts, float ratio)
{
    if (ratio <= 1.0f)
        return accounts;
    return std::uint32_t(std::ceil(double(accounts) / double(ratio) - 1e-6));
}

#endif
