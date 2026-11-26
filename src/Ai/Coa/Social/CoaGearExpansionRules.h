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

// CoA 3.3.1: the factory's bags. The Portable Hole (24 slots, item level 80, Wrath) filled every bag slot of every bot,
// a Classic realm's too (2 970 bots wore four on Malfurion). There a bot gets the Traveler's Backpack (16 slots, the
// common bag of the original game; on the bench, 0.9 % of the bots held more than its 64 slots in their four bags).
// The id is set here only: the Bottomless Bag (14156, 18 slots) if the bench shows too many full bags.
constexpr uint32_t PORTABLE_HOLE = 51809;
constexpr uint32_t CLASSIC_BAG = 4500;

inline uint32_t FactoryBag(bool limitGearExpansion, uint32_t realmMaxLevel)
{
    return limitGearExpansion && realmMaxLevel <= 60 ? CLASSIC_BAG : PORTABLE_HOLE;
}

enum class BagSlot
{
    Keep,
    Fill,    // an empty bag slot gets the factory's bag
    Replace  // the bag there is destroyed, then the factory's bag takes its place
};

// A bag is only ever destroyed empty (a self-found bot keeps its bags' contents through a refresh). The Portable Hole
// of a random bot gives way to the Classic bag, at a factory pass as at a login; another bag, at a factory pass only
// (destroyOld) and only for a larger one, as before.
inline BagSlot ForSlot(uint32_t oldId, uint32_t oldSlots, bool oldEmpty, uint32_t target, uint32_t targetSlots,
                       bool destroyOld, bool randomBot)
{
    if (!oldId)
        return BagSlot::Fill;
    if (oldId == target || !oldEmpty)
        return BagSlot::Keep;
    if (oldId == PORTABLE_HOLE)
        return randomBot ? BagSlot::Replace : BagSlot::Keep;
    return destroyOld && oldSlots < targetSlots ? BagSlot::Replace : BagSlot::Keep;
}
}  // namespace CoaGearExpansionRules

#endif
