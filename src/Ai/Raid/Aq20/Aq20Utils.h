/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ20UTILS_H
#define PLAYERBOTS_AQ20UTILS_H

#include "GameObject.h"
#include "PlayerbotAIConfig.h"
#include "Unit.h"

// CoA Combat (lot 7, docs/AQ-ETAT-3.0.md): every Ahn'Qiraj fix of the session is off unless
// AiPlayerbot.CoaCombat.AhnQiraj.Enable is 1.
inline bool AqCoaFixesOn() { return sPlayerbotAIConfig.coaCombatAhnQirajEnable; }

namespace CoaAq20
{
    // BU2: Buru the Gorger (15370) casts Devastate (2112405, 4 s) at his prey: a 10 yd cone, 45 degrees each side.
    constexpr uint32 NPC_BURU = 15370;
    constexpr uint32 SPELL_DEVASTATE = 2112405;
    constexpr float DEVASTATE_YARDS = 10.0f;

    // OS1: Ossirian's Fury of the Sands stacks on anyone more than 30 yd from an active obelisk: a World Trigger
    // (12999) carrying 2112700. The bots keep 24 yd, inside the shelter with a margin.
    constexpr uint32 NPC_WORLD_TRIGGER = 12999;
    constexpr uint32 SPELL_OBELISK_ACTIVE = 2112700;
    constexpr float OBELISK_SHELTER_YARDS = 30.0f;
    constexpr float OBELISK_KEEP_YARDS = 24.0f;
}

class RaidAq20Utils
{
public:
    static bool IsOssirianBuffActive(Unit* ossirian);
    static int32 GetOssirianDebuffTimeRemaining(Unit* ossirian);
    static GameObject* GetNearestCrystal(Unit* ossirian);
    // Conquest of Azeroth rebuilt the fight without crystals; the crystal logic above
    // only applies to the stock fight.
    static bool IsCoaOssirian(Unit* ossirian);
    // CoA Combat (OS1): the active obelisk closest to `from` within 200 yd, or nullptr.
    static Unit* GetNearestActiveObelisk(Unit* from);
};

#endif
