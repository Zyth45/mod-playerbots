/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ONYHELPERS_H
#define PLAYERBOTS_ONYHELPERS_H

#include "Common.h"

class Player;
class Unit;

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

enum OnyxiaNpcs
{
    // The difficulty templates 110184/210184/310184 (Onyxia) and 36566 (whelp)
    // keep the base entry, so GetEntry() only ever returns these two.
    NPC_ONYXIA = 10184,
    NPC_ONYXIAN_WHELP = 11262,  // Summon Onyxia Whelp 17646 on CoA too
};

inline constexpr uint32 ONYXIAS_LAIR_MAP_ID = 249;
// boss_onyxia_coa.cpp: she lifts off at 85% and lands at 35%, not 65% and 40%.
// The landing wipes her threat list and she picks the highest threat once down.
inline constexpr float ONYXIA_PHASE_THREE_HEALTH_PCT = 35.0f;

// The main tank must lead a bot by this much before it may start an attack on
// her; a running auto-attack stops below half of it
inline constexpr float THREAT_LEAD = 4000.0f;
inline constexpr float THREAT_LEAD_KEEP = THREAT_LEAD / 2;
// Longest hold once she is on the ground, so a main tank who never gets to her
// cannot hold everyone
inline constexpr uint32 HOLD_LIMIT_MS = 10 * IN_MILLISECONDS;

bool IsOnyxia(Unit const* unit);
bool IsOnyxianWhelp(Unit const* unit);
bool MustHoldPhaseThreeThreat(Player* bot, Unit* onyxia, float requiredLead);
}  // namespace OnyxiaHelpers

#endif
