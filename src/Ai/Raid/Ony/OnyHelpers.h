/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ONYHELPERS_H
#define PLAYERBOTS_ONYHELPERS_H

#include "Define.h"

// Stock and Conquest of Azeroth (mod-coa-raid-difficulty) ids side by side.
// A cast is resolved through SpellDifficulty.dbc before it becomes the current
// spell, so every difficulty variant is listed.
namespace OnyxiaHelpers
{
enum OnyxiaSpells
{
    // Fireball in the air phase, aimed at a player
    SPELL_FIREBALL = 18392,                // SpellDifficulty row 347: 18392/18392/350121/350121
    SPELL_FIREBALL_DIFF_2_3 = 350121,
    SPELL_MASSIVE_FIREBALL_COA = 2108300,  // CoA (boss_onyxia_coa.cpp); no SpellDifficulty row
};

inline bool IsFireballCast(uint32 spellId)
{
    return spellId == SPELL_FIREBALL || spellId == SPELL_FIREBALL_DIFF_2_3 || spellId == SPELL_MASSIVE_FIREBALL_COA;
}
}  // namespace OnyxiaHelpers

#endif
