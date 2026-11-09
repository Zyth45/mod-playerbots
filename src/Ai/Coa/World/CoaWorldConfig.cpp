/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: the session's settings kept in PlayerbotAIConfig. Called by PlayerbotAIConfig::Initialize();
// a new setting of the session goes here (its member in the "CoA World" block of PlayerbotAIConfig.h).
// None yet: AiPlayerbot.CoaWorld.* are read by the invasion manager (CoaWorldInvasionMgr.cpp).

#include "Config.h"
#include "PlayerbotAIConfig.h"

void PlayerbotAIConfig::LoadCoaWorldConfig()
{
    coaBotPacketsInMapThread = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.BotPacketsInMapThread", false, false);
    coaActiveZones = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.ActiveZones.Enable", false, false);
    coaIdleBotAI = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.IdleBotAI.Enable", false, false);
    coaIdleBotAIIntervalMs = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaWorld.IdleBotAI.IntervalMs", 2000, false);
    coaSmartScaleAverage = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.SmartScaleAverage.Enable", false, false);
}
