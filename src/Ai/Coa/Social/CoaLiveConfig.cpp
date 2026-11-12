/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Live (session CoA SquidBot): the session's settings, AiPlayerbot.CoaBotSurname, CoaBotIdentify,
// CoaBotSurnameList and CoaLive.*. Called by PlayerbotAIConfig::Initialize(); a new setting of the session goes here.

#include "PlayerbotAIConfig.h"

#include "CoaFreshStartRules.h"
#include "CoaSurnameRules.h"
#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <string>
#include <vector>

void PlayerbotAIConfig::LoadCoaLiveConfig()
{
    {
        // 0/1 until 3.0, 0/1/2 since: a player's 0 or 1 reads the same. Any other value is the default, 1.
        int32 const surname = sConfigMgr->GetOption<int32>("AiPlayerbot.CoaBotSurname", COA_SURNAME_BOT);
        coaBotSurname = CoaSurnameModeOf(surname);
        if (coaBotSurname != static_cast<uint32>(surname))
            LOG_ERROR("server.loading", "AiPlayerbot.CoaBotSurname = {} is not 0, 1 or 2: read as 1.", surname);
    }
    coaBotIdentify = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaBotIdentify", true);
    coaLiveEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Enable", true);
    coaLiveTypoChance = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.TypoChance", 4));
    coaLiveAbbrevChance = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.AbbrevChance", 25));
    coaLiveRareChance = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.RareChance", 10));
    coaLiveNoRepeatBot = std::min<uint32>(1000, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.NoRepeatBot", 50));
    coaLiveNoRepeatChannel =
        std::min<uint32>(1000, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.NoRepeatChannel", 200));
    coaLiveTypingMsPerCharMin = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.TypingMsPerCharMin", 60);
    coaLiveTypingMsPerCharMax = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.TypingMsPerCharMax", 90);
    coaLiveGreetRange = std::min<uint32>(40, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.GreetRange", 15));
    coaLiveSessionsEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Sessions.Enable", false);
    coaLiveSocialEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Social.Enable", true);
    coaLiveSocialInviteCooldownMinutes =
        std::max<uint32>(1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.Social.InviteCooldownMinutes", 60));
    coaLiveCityEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.City.Enable", true);
    coaLiveSheathEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Sheath.Enable", true);
    coaLiveCleanWorldEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.CleanWorld.Enable", true);
    coaLiveNightBankEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.NightBank.Enable", true);
    coaLiveTradeMessagesPerMinute =
        std::min<uint32>(6, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.TradeMessagesPerMinute", 1));
    coaLiveNoAwayFlag = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.NoAwayFlag", true);
    coaLiveChatEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Chat.Enable", true);
    coaLiveLegacyChat = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.LegacyChat", false);
    coaLiveHeavyRefreshPerTick = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.HeavyRefreshPerTick", 0);
    coaLiveMaintenanceSpread = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.MaintenanceSpread", true);
    coaLiveTrainerDedup = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.TrainerDedup", false);
    coaLiveTalentsPerUpdate = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.TalentsPerUpdate", 2);
    // Hidden test key (not in the .conf.dist nor in the launcher): the channels speak as if a real player stood
    // in every capital, so a bench without a human can prove the Trade channel.
    coaLiveDebugIgnoreRealPlayer = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Debug.IgnoreRealPlayer", false, false);
    coaLiveImmersionEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Immersion.Enable", true);
    coaLiveImmersionFishingPerThousand =
        std::min<uint32>(1000, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.Immersion.FishingPerThousand", 25));
    coaFreshStartEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaFreshStart.Enable", false);
    coaCatchUpEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaFreshStart.CatchUp.Enable", true);
    coaCatchUpMarginLevels =
        std::min<uint32>(20, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaFreshStart.CatchUp.MarginLevels", 3));
    coaCatchUpStepPercent =
        std::min<uint32>(200, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaFreshStart.CatchUp.StepPercent", 25));
    coaCatchUpMaxRate =
        std::clamp(sConfigMgr->GetOption<float>("AiPlayerbot.CoaFreshStart.CatchUp.MaxRate", 4.0f), 1.0f, 10.0f);
    coaCatchUpMinHumans =
        std::max<uint32>(1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaFreshStart.CatchUp.MinHumans", 1));
    coaCatchUpMaxTotalRate =
        std::clamp(sConfigMgr->GetOption<float>("AiPlayerbot.CoaFreshStart.CatchUp.MaxTotalRate", 6.0f), 0.0f, 20.0f);
    coaCatchUpActiveDays =
        std::clamp<uint32>(sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaFreshStart.CatchUp.ActiveDays", 7), 1, 90);
    coaCatchUpServiceAccounts = CoaFreshStart::ServiceAccounts(
        sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaFreshStart.CatchUp.ServiceAccounts", "AHBOT,DCDRIVER"));
    coaCatchUpCountGameMasters = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaFreshStart.CatchUp.CountGameMasters", true);
    // The Dungeon Clear driver under another name (mod-dungeon-clear, DungeonClear.TestRun.DriverAccount).
    for (std::string const& driver : CoaFreshStart::ServiceAccounts(
             sConfigMgr->GetOption<std::string>("DungeonClear.TestRun.DriverAccount", "dcdriver", false)))
        if (std::find(coaCatchUpServiceAccounts.begin(), coaCatchUpServiceAccounts.end(), driver) ==
            coaCatchUpServiceAccounts.end())
            coaCatchUpServiceAccounts.push_back(driver);
    coaSoulEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaSoul.Enable", true);
    coaPetsAutoBooks = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaPets.AutoBooks", false);
    coaSoulAddon = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaSoul.Addon", true);
    coaSoulWeights = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaSoul.Weights", "20,40,20,10,10");
    coaLiveDataFileEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.DataFile.Enable", true);
    // Shared with the other sessions' data files (the Builder adds them to the .dist once).
    coaLiveDataDir = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaDataDir", "", false);
    coaLiveDataFiles = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaDataFiles", true, false);
    {
        std::vector<std::string> rejected;
        coaBotSurnameList =
            CoaParseSurnameList(sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaBotSurnameList", ""), &rejected);
        for (std::string const& word : rejected)
            LOG_ERROR("server.loading", "AiPlayerbot.CoaBotSurnameList: \"{}\" left out (2 to 12 letters, no letter "
                      "three times in a row).", word);
    }
}
