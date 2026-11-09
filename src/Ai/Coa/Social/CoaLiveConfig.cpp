/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Live (session CoA SquidBot): the session's settings, AiPlayerbot.CoaBotSurname, CoaBotIdentify,
// CoaBotSurnameList and CoaLive.*. Called by PlayerbotAIConfig::Initialize(); a new setting of the session goes here.

#include "PlayerbotAIConfig.h"

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
    coaBotIdentify = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaBotIdentify", false);
    coaLiveEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Enable", false);
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
    coaLiveSocialEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Social.Enable", false);
    coaLiveSocialInviteCooldownMinutes =
        std::max<uint32>(1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.Social.InviteCooldownMinutes", 60));
    coaLiveCityEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.City.Enable", false);
    coaLiveSheathEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Sheath.Enable", false);
    coaLiveCleanWorldEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.CleanWorld.Enable", false);
    coaLiveNightBankEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.NightBank.Enable", false);
    coaLiveTalentsPerUpdate = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.TalentsPerUpdate", 0);
    // Hidden test key (not in the .conf.dist nor in the launcher): the channels speak as if a real player stood
    // in every capital, so a bench without a human can prove the Trade channel.
    coaLiveDebugIgnoreRealPlayer = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Debug.IgnoreRealPlayer", false, false);
    coaLiveImmersionEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Immersion.Enable", false);
    coaLiveImmersionFishingPerThousand =
        std::min<uint32>(1000, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.Immersion.FishingPerThousand", 25));
    coaFreshStartEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaFreshStart.Enable", false);
    coaSoulEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaSoul.Enable", false);
    coaPetsAutoBooks = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaPets.AutoBooks", false);
    coaSoulAddon = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaSoul.Addon", false);
    coaSoulWeights = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaSoul.Weights", "20,40,20,10,10");
    coaLiveDataFileEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.DataFile.Enable", false);
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
