#include "CoaWorldChatterRules.h"

#include <cassert>
#include <cstring>

using namespace CoaWorldChatterRules;

int main()
{
    // The ceilings: a line again only after its pause.
    assert(Due(0, 100, RARE_SECONDS));
    assert(!Due(100, 100 + RARE_SECONDS - 1, RARE_SECONDS));
    assert(Due(100, 100 + RARE_SECONDS, RARE_SECONDS));
    assert(NIGHT_SECONDS == 45 * 60 && WEATHER_SECONDS == 30 * 60 && SEASON_SECONDS == 3600);
    assert(FISHING_SECONDS == 20 * 60 && WORLD_BOSS_SECONDS == 30 * 60);

    // Midnight to six.
    assert(NightHour(0) && NightHour(3) && NightHour(5));
    assert(!NightHour(6) && !NightHour(23) && !NightHour(12));

    // The weather: rain, snow, storm; fog and fine say nothing.
    assert(WeatherOf(3) == Sky::RAIN && WeatherOf(5) == Sky::RAIN && WeatherOf(86) == Sky::RAIN);
    assert(WeatherOf(6) == Sky::SNOW && WeatherOf(7) == Sky::SNOW);
    assert(WeatherOf(8) == Sky::STORM && WeatherOf(22) == Sky::STORM && WeatherOf(42) == Sky::STORM);
    assert(WeatherOf(0) == Sky::NONE && WeatherOf(1) == Sky::NONE);
    assert(!std::strcmp(WeatherSituation(Sky::SNOW), "weather_snow"));
    assert(!std::strcmp(WeatherSituation(Sky::STORM), "weather_storm"));
    assert(WeatherSituation(Sky::NONE) == nullptr);

    // Ranks: rare and rare elite, not normal, elite or world boss.
    assert(RareRank(2) && RareRank(4));
    assert(!RareRank(0) && !RareRank(1) && !RareRank(3));
    assert(WorldBossRank(3) && !WorldBossRank(2));

    // 2 or 3 speakers, fewer only when fewer bots are there.
    assert(Speakers(10, 0) == 2 && Speakers(10, 1) == 3 && Speakers(1, 1) == 1 && Speakers(0, 0) == 0);

    // The holidays and their names.
    assert(SeasonHolidays().size() == 6);
    assert(!std::strcmp(SeasonName(HOLIDAY_HALLOWS_END, false), "Hallow's End"));
    assert(!std::strcmp(SeasonName(HOLIDAY_DARKMOON_THUNDER, true), "la foire de Sombrelune"));
    assert(SeasonName(1, false) == nullptr);

    // The lines, a few seconds apart.
    std::vector<time_t> const t = Spread({ 10, 10, 11 });
    assert(t[0] == 10 && t[1] == 12 && t[2] == 14);
    return 0;
}
