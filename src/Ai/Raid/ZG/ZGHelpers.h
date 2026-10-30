/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ZGHELPERS_H
#define PLAYERBOTS_ZGHELPERS_H

#include "Define.h"

#include <cmath>

class Player;

namespace ZulGurubHelpers
{
constexpr uint32 ZG_MAP_ID = 309;

enum class ZulGurubSpells : uint32
{
    // Hakkar
    SPELL_POISONOUS_BLOOD = 24321,
    SPELL_POOL_OF_CORRUPTED_BLOOD = 2118606,  // CoA: left where Corrupted Blood ends, 2 yd, +100% damage from Hakkar
    SPELL_POOL_OF_POISONED_BLOOD = 2118675,   // CoA: where a Son of Hakkar dies, 5 yd
    SPELL_WILL_OF_HAKKAR_CONTROL = 2118634,   // CoA: the controlled player, 15 s
    SPELL_WILL_OF_HAKKAR_SHIELD = 2118635,    // CoA: absorbs everything while controlled

    // Bloodlord Mandokir
    SPELL_THREATENING_GAZE = 24314,
};

enum class ZulGurubNpcs : uint32
{
    // Hakkar
    NPC_HAKKAR = 14834,
    NPC_SON_OF_HAKKAR = 11357,
    NPC_COA_HEART_OF_HAKKAR = 9780022,  // CoA: exposed during Blood Siphon, damage to it goes to Hakkar and ends the siphon

    // High Priest Thekal
    NPC_HIGH_PRIEST_THEKAL = 14509,
    NPC_ZEALOT_LORKHAN     = 11347,
    NPC_ZEALOT_ZATH        = 11348,

    // Bloodlord Mandokir
    NPC_BLOODLORD_MANDOKIR = 11382,
};

// CoA Blood Scythe (2118630, every 12 s): 150% weapon damage to every player within 20 yd in a 90 degree
// frontal cone, with Hakkar's Power and the Aspects on top. Non-tanks keep out of a wider cone.
constexpr float HAKKAR_SCYTHE_DANGER_RANGE = 25.0f;
constexpr float HAKKAR_SCYTHE_DANGER_ARC = 140.0f * float(M_PI) / 180.0f;  // total arc, 70 degrees each side
constexpr float HAKKAR_SCYTHE_FLANK_ANGLE = 110.0f * float(M_PI) / 180.0f;

// A member that goes down is restored to full health 10 s later (a zealot's death also restores Thekal) unless
// all three are down by then.
// At 15% a member can be finished inside that window.
constexpr float THEKAL_TRIO_SYNC_HEALTH_PCT = 15.0f;

// Mandokir charges the gazed player if they cause him any threat from the start of his Threatening Gaze cast until
// the aura ends.
bool IsGazedByMandokir(Player* bot);
}  // namespace ZulGurubHelpers

#endif
