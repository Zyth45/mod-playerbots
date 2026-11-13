/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: when the bots remark on the world around a real player (no game type, tested in
// tests/coa_world_chatter_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDCHATTERRULES_H
#define PLAYERBOTS_COAWORLDCHATTERRULES_H

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <vector>

namespace CoaWorldChatterRules
{
// The ceilings of IA MIND's study (IDEES-MONDE-VIVANT): the world stays calm.
constexpr time_t WORLD_BOSS_SECONDS = 30 * 60;  // per boss
constexpr time_t RARE_SECONDS = 20 * 60;        // per zone
constexpr time_t SEASON_SECONDS = 60 * 60;      // per zone
constexpr time_t NIGHT_SECONDS = 45 * 60;       // per guild
constexpr time_t WEATHER_SECONDS = 30 * 60;     // per zone
constexpr time_t FISHING_SECONDS = 20 * 60;     // per player

inline bool Due(time_t last, time_t now, time_t seconds) { return !last || now - last >= seconds; }

// Late evening at the server: midnight to six.
inline bool NightHour(int hour) { return hour >= 0 && hour < 6; }

// The weather of a zone, by the core's WeatherState: rain, snow, or a storm (sand, heavy snow).
enum class Sky : uint8_t { NONE, RAIN, SNOW, STORM };
inline Sky WeatherOf(uint32_t state)
{
    switch (state)
    {
        case 3:   // light rain
        case 4:   // medium rain
        case 5:   // heavy rain
        case 86:  // thunders
        case 90:  // black rain
            return Sky::RAIN;
        case 6:  // light snow
        case 7:  // medium snow
            return Sky::SNOW;
        case 8:    // heavy snow
        case 22:   // light sandstorm
        case 41:   // medium sandstorm
        case 42:   // heavy sandstorm
        case 106:  // black snow
            return Sky::STORM;
        default:
            return Sky::NONE;
    }
}
inline char const* WeatherSituation(Sky sky)
{
    switch (sky)
    {
        case Sky::RAIN:
            return "weather_rain";
        case Sky::SNOW:
            return "weather_snow";
        case Sky::STORM:
            return "weather_storm";
        default:
            return nullptr;
    }
}

// A rare creature (CreatureEliteType: 2 rare elite, 4 rare), not a world boss (3).
inline bool RareRank(uint32_t rank) { return rank == 2 || rank == 4; }
inline bool WorldBossRank(uint32_t rank) { return rank == 3; }

// How many bots speak when the line answers a real player (a battleground's end, a world boss): 2 or 3, fewer only
// when fewer are there.
inline size_t Speakers(size_t there, uint32_t extra)
{
    return std::min<size_t>(there, 2 + std::min<uint32_t>(extra, 1));
}

// The holidays of the season lines (HolidayIds), and their name in the bots' language.
constexpr uint32_t HOLIDAY_WINTER_VEIL = 141;
constexpr uint32_t HOLIDAY_HALLOWS_END = 324;
constexpr uint32_t HOLIDAY_BREWFEST = 372;
constexpr uint32_t HOLIDAY_DARKMOON_ELWYNN = 374;
constexpr uint32_t HOLIDAY_DARKMOON_THUNDER = 375;
constexpr uint32_t HOLIDAY_DARKMOON_SHATTRATH = 376;
inline std::vector<uint32_t> SeasonHolidays()
{
    return { HOLIDAY_HALLOWS_END, HOLIDAY_WINTER_VEIL, HOLIDAY_BREWFEST, HOLIDAY_DARKMOON_ELWYNN,
             HOLIDAY_DARKMOON_THUNDER, HOLIDAY_DARKMOON_SHATTRATH };
}
inline char const* SeasonName(uint32_t holiday, bool french)
{
    switch (holiday)
    {
        case HOLIDAY_HALLOWS_END:
            return french ? "la Sanssaint" : "Hallow's End";
        case HOLIDAY_WINTER_VEIL:
            return french ? "le Voile d'hiver" : "Winter Veil";
        case HOLIDAY_BREWFEST:
            return french ? "la Fête des Brasseurs" : "Brewfest";
        case HOLIDAY_DARKMOON_ELWYNN:
        case HOLIDAY_DARKMOON_THUNDER:
        case HOLIDAY_DARKMOON_SHATTRATH:
            return french ? "la foire de Sombrelune" : "the Darkmoon Faire";
        default:
            return nullptr;
    }
}

// The times of the lines: each a few seconds after the event, sorted, at least minGap apart.
inline std::vector<time_t> Spread(std::vector<time_t> times, time_t minGap = 2)
{
    std::sort(times.begin(), times.end());
    for (size_t i = 1; i < times.size(); ++i)
        times[i] = std::max(times[i], times[i - 1] + minGap);
    return times;
}
}  // namespace CoaWorldChatterRules

#endif
