/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: the session's settings kept in PlayerbotAIConfig. Called by PlayerbotAIConfig::Initialize();
// a new setting of the session goes here (its member in the "CoA World" block of PlayerbotAIConfig.h).
// Most AiPlayerbot.CoaWorld.* are read by their own bricks (src/Ai/Coa/World).

#include "Config.h"
#include "PlayerbotAIConfig.h"

void PlayerbotAIConfig::LoadCoaWorldConfig()
{
    coaDpsTargetCache = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.DpsTargetCache", false, false);
}
