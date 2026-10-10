/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BWLHELPERS_H
#define PLAYERBOTS_BWLHELPERS_H

#include "Player.h"
#include "PlayerbotAI.h"

namespace BlackwingLairHelpers
{
    enum class BlackwingLairSpells : uint32
    {
        // General
        SPELL_ONYXIA_SCALE_CLOAK = 22683,

        // Razorgore the Untamed (CoA keeps the stock Orb of Domination, same id)
        SPELL_MINDCONTROL = 19832,

        // Vaelastrasz the Corrupt
        SPELL_BURNING_ADRENALINE = 18173,

        // Chromaggus
        SPELL_BROOD_AFFLICTION_BRONZE = 23170,
        SPELL_HOURGLASS_SAND = 23645,

        // Nefarian
        SPELL_WILD_MAGIC = 23410,

        // Conquest of Azeroth (mod-coa-raid-difficulty). Ids without a SpellDifficulty
        // row are the same on all four difficulties.

        // Nefarian: 22683 or 2111263 protect from Shadow Flame (boss_nefarian_coa.cpp)
        SPELL_BLACK_DRAGON_SCALE_CLOAK_COA = 2111263,

        // Vaelastrasz: the aura on the player, then its stack aura (SpellDifficulty row 5111)
        SPELL_BURNING_ADRENALINE_COA = 2110621,
        SPELL_BURNING_ADRENALINE_COA_STACK_D0 = 2110622,
        SPELL_BURNING_ADRENALINE_COA_STACK_D1 = 2110623,
        SPELL_BURNING_ADRENALINE_COA_STACK_D2 = 2110624,
        SPELL_BURNING_ADRENALINE_COA_STACK_D3 = 2110625,

        // Chromaggus: bronze affliction, the bronze blessing that prevents its removal,
        // and the Hourglass Sand that item 19183 casts (boss_chromaggus_coa.cpp)
        SPELL_BROOD_AFFLICTION_BRONZE_COA = 2111016,
        SPELL_DRAGONFLIGHT_BLESSING_BRONZE_COA = 2111011,
        SPELL_HOURGLASS_SAND_COA = 2111023
    };

    enum class BlackwingLairGameObjects : uint32
    {
        // General
        GO_SUPPRESSION_DEVICE = 179784,

        // Razorgore the Untamed
        GO_BLACK_DRAGON_EGG = 177807
    };

    enum class BlackwingLairNPCs : uint32
    {
        // Trash
        NPC_DEATH_TALON_WYRMGUARD = 12460
    };

    bool IsActiveSuppressionDeviceInRange(GameObject const* go, Player const* bot);
    bool AreRazorgoreEggsAlive(PlayerbotAI* botAI);
    bool IsRazorgoreOffTank(Player* bot);
    bool IsNonBABotNearPosition(Player const* bot, Position const& position, float distance);

    // Stock or CoA: the unit carries Burning Adrenaline.
    bool HasBurningAdrenaline(Unit const* unit);
    // Stock or CoA: the unit wears a cloak aura that protects from Shadow Flame.
    bool HasShadowFlameCloak(Unit const* unit);
    // Stock or CoA: the unit carries Brood Affliction: Bronze that Hourglass Sand
    // can remove (CoA: not while blessed by the bronze dragonflight).
    bool HasCurableBroodAfflictionBronze(Unit const* unit);
    // The Hourglass Sand spell that cures the affliction the unit carries.
    uint32 GetHourglassSandSpell(Unit const* unit);
}

#endif
