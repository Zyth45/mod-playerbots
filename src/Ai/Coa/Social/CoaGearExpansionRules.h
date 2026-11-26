/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAGEAREXPANSIONRULES_H
#define _PLAYERBOT_COAGEAREXPANSIONRULES_H

#include <cstdint>

// AiPlayerbot.LimitGearExpansion: the gear of a later expansion a bot of the factory never gets.
namespace CoaGearExpansionRules
{
constexpr uint32_t FIRST_TBC_ITEM = 23728;    // approx. first item of the Burning Crusade (patch 2.0)
constexpr uint32_t FIRST_WOTLK_ITEM = 35570;  // approx. first item of Wrath of the Lich King (patch 3.0)
constexpr uint32_t QUALITY_RARE = 3;
constexpr uint32_t LAST_BLIZZARD_ITEM = 56806;  // the last item of Wrath of the Lich King; above it, CoA's
// CoA 3.3.1: the original game's green and blue gear tops at item level 72 (Drake Scale Drape). Below the first
// Burning Crusade id, the only greens above it are of that expansion: Fel Iron Chain Coif (23493) and Mirren's
// Drinking Hat (23587), item level 90 for level 60, worn by a level 60 bot of a Classic realm. The epics of
// Ahn'Qiraj and Naxxramas (item level up to 92) and the PvP gear of patch 1.12 stay.
constexpr uint32_t CLASSIC_TOP_RARE_ITEM_LEVEL = 72;

// By the bot's level, as before; the greens and blues above the original game's top only on a realm whose level
// ceiling (MaxPlayerLevel) is 60 or less: a Dawnrise or Wrath realm keeps them. A Worldforged item (CoA, found in the
// open world) is never of a later expansion.
inline bool LaterExpansion(uint32_t itemId, uint32_t quality, uint32_t itemLevel, uint32_t botLevel, uint32_t realmMaxLevel,
                           bool worldforged)
{
    if (worldforged)
        return false;
    if (botLevel <= 60)
        return itemId >= FIRST_TBC_ITEM ||
               (realmMaxLevel <= 60 && quality <= QUALITY_RARE && itemLevel > CLASSIC_TOP_RARE_ITEM_LEVEL);
    if (botLevel <= 70)
        // The step from the Burning Crusade to Wrath is less clear: three of its items stand above it.
        return itemId >= FIRST_WOTLK_ITEM && itemId != 36737 && itemId != 37739 && itemId != 37740;
    return false;
}
}  // namespace CoaGearExpansionRules

#endif
