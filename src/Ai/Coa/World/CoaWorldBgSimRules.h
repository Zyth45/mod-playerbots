/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of the simulated battlegrounds (no game type, tested in tests/coa_world_bg_sim_rules.cpp).
// The honor follows the core's battlegrounds (Battleground::GetBonusHonorFromKill, Acore::Honor::hk_honor_at_level).

#ifndef PLAYERBOTS_COAWORLDBGSIMRULES_H
#define PLAYERBOTS_COAWORLDBGSIMRULES_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace CoaWorldBgSimRules
{
// Acore::Honor::hk_honor_at_level: honor of "kills" kills at the battleground's highest level (capped at 80).
inline uint32_t KillsHonor(uint32_t maxLevel, float kills)
{
    return uint32_t(std::ceil(kills * float(std::min<uint32_t>(maxLevel, 80)) * 1.55f));
}

// The Alliance's chance (percent) to win: even, a little more for the higher average level, 30 to 70.
inline uint32_t AllianceWinPct(float allianceLevel, float hordeLevel)
{
    float const pct = 50.0f + (allianceLevel - hordeLevel) * 3.0f;
    return uint32_t(std::clamp(pct, 30.0f, 70.0f));
}

// Honor of one player at the end of a simulated match.
// endKills: CONFIG_BG_REWARD_WINNER/LOSER_HONOR_FIRST or _LAST, as the core gives at the end.
// objectives: the team's bonus "kills" during the match (a Warsong flag capture is 2, an Arathi tick of 260
// resources is 1). hks / share: the player's honorable kills, each shared with share players.
inline uint32_t MatchHonor(uint32_t maxLevel, uint32_t endKills, uint32_t objectives, uint32_t hks, uint32_t share)
{
    uint32_t const perKill = KillsHonor(maxLevel, 1.0f);
    return KillsHonor(maxLevel, float(endKills + objectives)) + hks * perKill / std::max<uint32_t>(1, share);
}

// Arathi Basin: honor ticks of a final score (one per 260 resources, as BG_AB_HONOR_TICK_NORMAL).
inline uint32_t ArathiTicks(uint32_t score) { return score / 260; }

// A minute count between min and max from a roll in [0, 100].
inline uint32_t MatchMinutes(uint32_t min, uint32_t max, uint32_t roll)
{
    if (max <= min)
        return min;
    return min + (max - min) * std::min<uint32_t>(roll, 100) / 100;
}

// A side has enough players: at least half of the team size, and at least 2.
inline bool EnoughPerSide(uint32_t have, uint32_t teamSize) { return have >= std::max<uint32_t>(2, (teamSize + 1) / 2); }
}  // namespace CoaWorldBgSimRules

#endif
