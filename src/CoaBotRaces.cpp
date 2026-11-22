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
#include <vector>

// A core with added races may declare which of them have a male model only. No core does today: the module's own
// list (CoaBotRaceRules.h) and the server's lines are the only sources. A header's presence proves nothing (3.2: the
// official core added an AscensionIncarnation.h of its own, without that function, and the build broke), so the
// core path is taken only when the core defines COA_CORE_MALE_ONLY_RACES_HEADER, the header declaring
// IsAscensionMaleOnlyRace(uint8). COA_BOT_RACES_FORCE_FALLBACK keeps the fallback even then.
#if defined(COA_CORE_MALE_ONLY_RACES_HEADER) && !defined(COA_BOT_RACES_FORCE_FALLBACK)
#include COA_CORE_MALE_ONLY_RACES_HEADER
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
        std::vector<uint32> const races = CoaBotRace::RacesOfKey(key, PREFIX);
        if (races.empty())
        {
            LOG_ERROR("server.loading", "{}: not race ids from 1 to {} (\"85\", or \"20/29\" for one id per side), "
                      "line ignored.", key, CoaBotRace::MAX_RACE);
            continue;
        }
        std::string error;
        CoaBotRace::Profile profile;
        if (!CoaBotRace::Parse(sConfigMgr->GetOption<std::string>(key, "", false), profile, error))
        {
            LOG_ERROR("server.loading", "{}: {}, line ignored.", key, error);
            continue;
        }
        // A race id this server does not have is skipped alone: the line may come from a list made for a newer
        // version of the races.
        for (uint32 race : races)
        {
            if (!sChrRacesStore.LookupEntry(race))
            {
                LOG_ERROR("server.loading", "{}: race {} is not in ChrRaces.dbc on this server, skipped.", key, race);
                continue;
            }
            profiles[race] = profile;
            ++loaded;
        }
    }
    if (loaded)
        LOG_INFO("server.loading", "Playerbots: {} races described by AiPlayerbot.BotRace.* lines.", loaded);
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
