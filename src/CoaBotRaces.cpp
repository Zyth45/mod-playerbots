/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaBotRaces.h"

#include "Config.h"
#include "DBCStores.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"

#include <array>

// A core with added races may declare which of them have a male model only. The official core has no such header:
// the module's own list (CoaBotRaceRules.h) and the server's lines are then the only sources.
// COA_BOT_RACES_FORCE_FALLBACK builds the fallback on a core that has it, to test both paths.
#if __has_include("AscensionIncarnation.h") && !defined(COA_BOT_RACES_FORCE_FALLBACK)
#include "AscensionIncarnation.h"
#define COA_BOT_RACES_CORE_MALE_ONLY 1
#else
#define COA_BOT_RACES_CORE_MALE_ONLY 0
#endif

namespace
{
std::array<CoaBotRace::Profile, CoaBotRace::MAX_RACE + 1> profiles;
char const* const PREFIX = "AiPlayerbot.BotRace.";
}

namespace CoaBotRaces
{
void Load()
{
    profiles.fill(CoaBotRace::Profile());
    uint32 loaded = 0;
    for (std::string const& key : sConfigMgr->GetKeysByString(PREFIX))
    {
        uint32 const race = CoaBotRace::RaceOfKey(key, PREFIX);
        if (!race)
        {
            LOG_ERROR("server.loading", "{}: not a race id from 1 to {}, line ignored.", key, CoaBotRace::MAX_RACE);
            continue;
        }
        if (!sChrRacesStore.LookupEntry(race))
        {
            LOG_ERROR("server.loading", "{}: race {} is not in ChrRaces.dbc on this server, line ignored.", key, race);
            continue;
        }
        std::string error;
        CoaBotRace::Profile profile;
        if (!CoaBotRace::Parse(sConfigMgr->GetOption<std::string>(key, "", false), profile, error))
        {
            LOG_ERROR("server.loading", "{}: {}, line ignored.", key, error);
            continue;
        }
        profiles[race] = std::move(profile);
        ++loaded;
    }
    if (loaded)
        LOG_INFO("server.loading", "Playerbots: {} race lines read (AiPlayerbot.BotRace.*).", loaded);
}

CoaBotRace::Profile const& Get(std::uint32_t race)
{
    static CoaBotRace::Profile const none;
    return race <= CoaBotRace::MAX_RACE ? profiles[race] : none;
}

bool CoreMaleOnly([[maybe_unused]] std::uint8_t race)
{
#if COA_BOT_RACES_CORE_MALE_ONLY
    return IsAscensionMaleOnlyRace(race);
#else
    return false;
#endif
}

bool Allowed(std::uint32_t race)
{
    return CoaBotRace::Allowed(race, Get(race), sPlayerbotAIConfig.customRacesForBots, sPlayerbotAIConfig.excludedBotRaces);
}
}  // namespace CoaBotRaces
