/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Combat: the session's settings kept in PlayerbotAIConfig. Called by PlayerbotAIConfig::Initialize();
// a new setting of the session goes here (its member in the "CoA Combat" block of PlayerbotAIConfig.h).

#include <algorithm>
#include "CoaCombatPurgeRule.h"
#include "Config.h"
#include "PlayerbotAIConfig.h"

void PlayerbotAIConfig::LoadCoaCombatConfig()
{
    coaCombatBreathGuardEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.BreathGuard.Enable", true);
    coaCombatStuckCombatEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.StuckCombat.Enable", true);
    // Dungeon Clear's old key first: a value the player set there is kept.
    float const legacyStuckCombat = sConfigMgr->GetOption<float>("DungeonClear.StuckCombatTimeout", -1.0f, false);
    coaCombatStuckCombatTimeoutSeconds =
        legacyStuckCombat >= 0.0f
            ? legacyStuckCombat
            : sConfigMgr->GetOption<float>("AiPlayerbot.CoaCombat.StuckCombat.TimeoutSeconds", 15.0f);
    coaCombatCombatPurgeEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.CombatPurge.Enable", true);
    coaCombatCombatPurgeWindowMs = CoaCombatPurgeRule::WindowMs(
        sConfigMgr->GetOption<int64>("DungeonClear.UnreachableCombatPurgeSecs", -1, false));
    coaCombatSunkenTempleEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.SunkenTemple.Enable", true);
    coaCombatTelegraphEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.Telegraph.Enable", false);
    coaCombatPursuitStopEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.PursuitStop.Enable", false);
    coaCombatOffTankEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.OffTank.Enable", false);
    coaCombatGearScoreEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.GearScore.Enable", true);
    coaCombatLootNeedWaitMs = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaCombat.LootNeedWaitSeconds", 20) * 1000;
    coaCombatMoltenCoreEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.MoltenCore.Enable", true);
    coaCombatBlackwingLairEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.BlackwingLair.Enable", true);
    coaCombatZulGurubEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.ZulGurub.Enable", true);
    coaCombatAhnQirajEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.AhnQiraj.Enable", true);
    coaCombatLootNeedMinUpgradePct =
        std::max(0.0f, sConfigMgr->GetOption<float>("AiPlayerbot.CoaCombat.LootNeedMinUpgradePct", 10.0f));
    coaCombatLootNeedEpicOnly = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.LootNeedEpicOnly", false);
    coaCombatCallOfTheGraveEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.CallOfTheGrave.Enable", true);
    coaCombatPlayerLedEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.PlayerLed.Enable", true);
    coaCombatBenchKeyOnly = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.BenchKeyOnly", false);
    coaCombatPullGuardSightEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.PullGuardSight.Enable", true);
    coaCombatPullGuardSightStrict = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.PullGuardSight.Strict", false);
    coaCombatGearPvpSoldOnly = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.GearPvpSoldOnly", true);
    coaCombatLustBossOnly = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaCombat.LustBossOnly", true);
    coaCombatLustBossHealthPct =
        std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaCombat.LustBossHealthPct", 30));
}
