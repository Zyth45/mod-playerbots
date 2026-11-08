/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Live (session CoA SquidBot): the session's settings, AiPlayerbot.CoaBotSurname, CoaBotIdentify,
// CoaBotSurnameList and CoaLive.*. Called by PlayerbotAIConfig::Initialize(); a new setting of the session goes here.

#include "PlayerbotAIConfig.h"

#include "CoaSessionRules.h"
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
    coaLiveCityEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.City.Enable", false);
    coaLiveImmersionEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Immersion.Enable", false);
    coaFreshStartEnable = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaFreshStart.Enable", false);
    {
        std::string const curve = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaLive.Sessions.HourCurve", "");
        CoaSessions::Curve parsed;
        if (!CoaSessions::ParseCurve(curve, parsed))
            LOG_ERROR("server.loading", "AiPlayerbot.CoaLive.Sessions.HourCurve: 24 numbers 0 to 100 are needed, "
                      "the default curve is used.");
        coaLiveSessionsCurve.assign(parsed.begin(), parsed.end());
    }
    coaLiveSessionsWeekendBonus =
        std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.Sessions.WeekendBonus", 10));
    coaLiveSessionsMinPercent =
        std::min<uint32>(100, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLive.Sessions.MinPercent", 20));
    coaLiveSessionsHourShift =
        std::clamp<int32>(sConfigMgr->GetOption<int32>("AiPlayerbot.CoaLive.Sessions.HourShift", 0), -23, 23);
    {
        std::vector<std::string> rejected;
        coaBotSurnameList =
            CoaParseSurnameList(sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaBotSurnameList", ""), &rejected);
        for (std::string const& word : rejected)
            LOG_ERROR("server.loading", "AiPlayerbot.CoaBotSurnameList: \"{}\" left out (2 to 12 letters, no letter "
                      "three times in a row).", word);
    }
}
