/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Combat: the session's settings kept in PlayerbotAIConfig. Called by PlayerbotAIConfig::Initialize();
// a new setting of the session goes here (its member in the "CoA Combat" block of PlayerbotAIConfig.h).

#include "Config.h"
#include "PlayerbotAIConfig.h"

void PlayerbotAIConfig::LoadCoaCombatConfig()
{
    coaCombatBreathGuardEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.BreathGuard.Enable", false);
    coaCombatStuckCombatEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.StuckCombat.Enable", false);
    // Dungeon Clear's old key first: a value the player set there is kept.
    float const legacyStuckCombat = sConfigMgr->GetOption<float>("DungeonClear.StuckCombatTimeout", -1.0f, false);
    coaCombatStuckCombatTimeoutSeconds =
        legacyStuckCombat >= 0.0f
            ? legacyStuckCombat
            : sConfigMgr->GetOption<float>("AiPlayerbot.CoaCombat.StuckCombat.TimeoutSeconds", 15.0f);
}
