/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: which bots join a real player's dungeon queue, by level (no game type, tested in
// tests/coa_world_lfg_level_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDLFGLEVELRULES_H
#define PLAYERBOTS_COAWORLDLFGLEVELRULES_H

#include <cstdint>
#include <ctime>

namespace CoaWorldLfgLevelRules
{
// Bots within NEAR_SPREAD levels of the players first; when the players have waited WIDE_AFTER_SECONDS, within
// WIDE_SPREAD (seen: a level 60 player in Blackrock Depths with four bots of level 55).
constexpr uint32_t NEAR_SPREAD = 2;
constexpr uint32_t WIDE_SPREAD = 5;
constexpr time_t WIDE_AFTER_SECONDS = 120;

inline uint32_t Spread(time_t waited) { return waited >= WIDE_AFTER_SECONDS ? WIDE_SPREAD : NEAR_SPREAD; }

// minLevel, maxLevel: the lowest and highest level of the real players in that queue; waited: the longest wait among
// them. The dungeon's own level range is checked apart.
inline bool NearPlayers(uint32_t botLevel, uint32_t minLevel, uint32_t maxLevel, time_t waited)
{
    uint32_t const spread = Spread(waited);
    return botLevel + spread >= minLevel && botLevel <= maxLevel + spread;
}
}  // namespace CoaWorldLfgLevelRules

#endif
