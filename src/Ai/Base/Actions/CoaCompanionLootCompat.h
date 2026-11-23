/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COACOMPANIONLOOTCOMPAT_H
#define _PLAYERBOT_COACOMPANIONLOOTCOMPAT_H

// The looting companion of the alts reads the core's read-only API (AscensionCompanionLoot.h) when the core has it.
// A core without it (the public Conquest-of-AzerothCore core) gets the same answers from the module: the creatures and
// spells below, and the active appearance read from the database the core writes on every change, through a cache.
// COA_FORCE_COMPANION_LOOT_FALLBACK builds the module's answers on a core that has the API (to test them).
// 3.2.1: a header's name proves nothing (the official core added an AscensionIncarnation.h of its own, without the
// function the module looked for, and the 3.2 build broke): the core's API is used only when the core defines
// COA_CORE_COMPANION_LOOT_HEADER, the header declaring Ascension::CompanionLooting.

#if defined(COA_CORE_COMPANION_LOOT_HEADER) && !defined(COA_FORCE_COMPANION_LOOT_FALLBACK)
#include COA_CORE_COMPANION_LOOT_HEADER
#define COA_HAS_CORE_COMPANION_LOOT 1
namespace CoaCompanionLoot = Ascension::CompanionLooting;
#else
#include <cstdint>

class Player;

namespace CoaCompanionLoot
{
constexpr std::uint32_t LootAppearanceCategory = 38;
constexpr std::uint32_t SkinningAppearanceCategory = 61;
constexpr std::uint32_t LootTransfiguratorAppearance = 47520;
constexpr std::uint32_t SkinPeelerAppearance = 639807;
constexpr std::uint32_t LootTransfiguratorSpell = 84419;
constexpr std::uint32_t SkinPeelerSpell = 92864;
constexpr std::uint32_t Lootbot3000Creature = 44022;
constexpr std::uint32_t KleptomanicDemonCreature = 79026;

constexpr bool IsSelfLootingCompanion(std::uint32_t creatureEntry)
{
    return creatureEntry == Lootbot3000Creature || creatureEntry == KleptomanicDemonCreature;
}

constexpr bool LootsWith(bool appearanceActive, bool selfLootingCompanionOut, bool inRaid)
{
    return appearanceActive || (selfLootingCompanionOut && !inRaid);
}

constexpr bool SkinsWith(bool appearanceActive) { return appearanceActive; }

[[nodiscard]] bool HasLootAppearance(Player const* player);
[[nodiscard]] bool HasSkinningAppearance(Player const* player);
[[nodiscard]] bool CompanionLoots(Player const* player);
[[nodiscard]] bool CompanionSkins(Player const* player);
[[nodiscard]] float LootRadius(Player const* player);
[[nodiscard]] float SkinningRadius(Player const* player);
}  // namespace CoaCompanionLoot
#endif

#endif
