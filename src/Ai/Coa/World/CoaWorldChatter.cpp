/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: the bots remark on the world around a real player (IA MIND's study IDEES-MONDE-VIVANT, the World part).
 * Each under its own key AiPlayerbot.CoaWorld.Chatter.*, all on by default:
 *   BgEnd      - a battleground ends with a real player in it: 2 or 3 bots of the player's side say gg in the
 *                battleground (bg_end_win, bg_end_loss).
 *   WorldBoss  - a real player fights a world boss: 2 or 3 bots of the zone near its level call for it in the zone
 *                channel (world_boss_call), once per boss and half hour.
 *   RareSeen   - a bot near a real player sees a rare creature alive and out of combat, and says so in the zone
 *                channel (rare_seen), once per zone and 20 minutes. The bot does not attack it for that.
 *   Season     - Hallow's End, Winter Veil, Brewfest or the Darkmoon Faire: a bot of a zone with a real player says
 *                a word of it in General (season_event, %event), once per zone and hour.
 *   Night      - midnight to six, server time: a bot of a real player's guild says a late word in guild chat
 *                (night_chat), once per guild and 45 minutes.
 *   Weather    - the weather of a zone turns to rain, snow or a storm: a bot within 40 yards of a real player remarks
 *                on it aloud (weather_rain, weather_snow, weather_storm), once per zone and half hour.
 *   Fishing    - a real player fishes: a bot within 20 yards says a word aloud (fishing_idle), once per player and
 *                20 minutes.
 * Lines from IA MIND's bank only: without one, nothing is said. Map threads (battleground end, weather) queue; the
 * world thread picks the bots and speaks.
 */

#include "CoaWorldBots.h"
#include "CoaWorldChatterRules.h"

#include "Battleground.h"
#include "CellImpl.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Weather.h"
#include "World.h"
#include "WorldSession.h"

#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorld;
using namespace CoaWorldChatterRules;

constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Chatter.";
constexpr float WEATHER_YARDS = 40.0f;
constexpr float FISHING_YARDS = 20.0f;
constexpr float RARE_BOT_YARDS = 150.0f;  // the bots near the real player who may see a rare
constexpr float RARE_SIGHT_YARDS = 40.0f;
constexpr uint32 BOSS_LEVEL_SPREAD = 5;

struct Settings
{
    bool bgEnd = true, worldBoss = true, rareSeen = true, season = true, night = true, weather = true, fishing = true;
};
Settings settings;

bool IsHuman(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player) &&
           !CoaWorld::IsTestDriver(player);
}

bool IsBot(Player* player) { return player && GET_PLAYERBOT_AI(player) && !IsHuman(player); }

// A bot that may speak now.
bool Free(Player* bot) { return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsInCombat(); }

bool OpenWorld(Player* player) { return player->GetMap() && !player->GetMap()->Instanceable(); }

std::string AreaName(uint32 areaId)
{
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    return area ? std::string(area->area_name[sWorld->GetDefaultDbcLocale()]) : std::string();
}

struct Line
{
    ObjectGuid bot;
    std::string situation;
    Slots slots;
    time_t at = 0;
};

class Chatter
{
public:
    static Chatter& Instance()
    {
        static Chatter instance;
        return instance;
    }

    // Map threads.
    void BattlegroundEnded(Battleground* bg, TeamId winner)
    {
        if (!settings.bgEnd || !bg || (winner != TEAM_ALLIANCE && winner != TEAM_HORDE))
            return;
        time_t const now = time(nullptr);
        std::vector<Line> lines;
        for (TeamId team : { TEAM_ALLIANCE, TEAM_HORDE })
        {
            bool human = false;
            std::vector<Player*> bots;
            for (auto const& [guid, player] : bg->GetPlayers())
            {
                if (!player || player->GetBgTeamId() != team)
                    continue;
                if (IsHuman(player))
                    human = true;
                else if (IsBot(player))
                    bots.push_back(player);
            }
            if (!human || bots.empty())
                continue;
            std::shuffle(bots.begin(), bots.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            std::vector<time_t> times;
            for (size_t i = 0, n = Speakers(bots.size(), urand(0, 1)); i < n; ++i)
                times.push_back(now + urand(2, 8));
            times = CoaWorldChatterRules::Spread(times);
            for (size_t i = 0; i < times.size(); ++i)
                lines.push_back({ bots[i]->GetGUID(), team == winner ? "bg_end_win" : "bg_end_loss", {}, times[i] });
        }
        std::lock_guard<std::mutex> guard(_lock);
        _lines.insert(_lines.end(), lines.begin(), lines.end());
    }

    void WeatherChanged(uint32 zone, uint32 state)
    {
        if (!settings.weather || WeatherOf(state) == Sky::NONE)
            return;
        std::lock_guard<std::mutex> guard(_lock);
        _weathers.push_back({ zone, state });
    }

    // World thread.
    void Update(uint32 diff)
    {
        _timer += diff;
        if (_timer < IN_MILLISECONDS)
            return;
        _timer = 0;
        ++_ticks;
        time_t const now = time(nullptr);

        std::vector<std::pair<uint32, uint32>> weathers;
        {
            std::lock_guard<std::mutex> guard(_lock);
            weathers.swap(_weathers);
        }
        std::vector<Player*> humans;
        for (Player* player : sRandomPlayerbotMgr.GetPlayers())
            if (IsHuman(player) && player->IsInWorld())
                humans.push_back(player);

        for (auto const& [zone, state] : weathers)
            OnWeather(humans, zone, state, now);
        if (_ticks % 15 == 0)
        {
            WorldBoss(humans, now);
            Fishing(humans, now);
        }
        if (_ticks % 60 == 0)
        {
            Rare(humans, now);
            Season(humans, now);
            Night(humans, now);
        }
        Speak(now);
    }

private:
    void Say(Player* bot, char const* situation, Slots slots, time_t at)
    {
        std::lock_guard<std::mutex> guard(_lock);
        _lines.push_back({ bot->GetGUID(), situation, std::move(slots), at });
    }

    // Says at once; true when the bank had a line.
    static bool SayNow(Player* bot, char const* situation, Slots const& slots)
    {
        return Free(bot) && BotLine(bot, situation, slots);
    }

    void Speak(time_t now)
    {
        std::vector<Line> due, later;
        {
            std::lock_guard<std::mutex> guard(_lock);
            for (Line& line : _lines)
                (line.at <= now ? due : later).push_back(std::move(line));
            _lines.swap(later);
        }
        for (Line const& line : due)
            if (Player* bot = ObjectAccessor::FindPlayer(line.bot); bot && bot->IsInWorld())
                BotLine(bot, line.situation.c_str(), line.slots);
    }

    // Random bots free to speak, in the player's map and zone, within yards of it (0: the whole zone).
    static std::vector<Player*> BotsNear(Player* player, float yards)
    {
        std::vector<Player*> bots;
        for (auto const& [guid, bot] : RandomBots())
            if (Free(bot) && bot->GetMapId() == player->GetMapId() && bot->GetZoneId() == player->GetZoneId() &&
                (!yards || bot->IsWithinDistInMap(player, yards)))
                bots.push_back(bot);
        std::shuffle(bots.begin(), bots.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
        return bots;
    }

    void WorldBoss(std::vector<Player*> const& humans, time_t now)
    {
        if (!settings.worldBoss)
            return;
        for (Player* player : humans)
        {
            Unit* victim = player->GetVictim();
            Creature* boss = victim ? victim->ToCreature() : nullptr;
            if (!boss || !boss->IsAlive() || !OpenWorld(player) || !WorldBossRank(boss->GetCreatureTemplate()->rank))
                continue;
            time_t& last = _boss[boss->GetGUID().GetCounter()];
            if (!Due(last, now, WORLD_BOSS_SECONDS))
                continue;
            std::vector<Player*> callers;
            for (Player* bot : BotsNear(player, 0))
                if (bot->GetLevel() + BOSS_LEVEL_SPREAD >= player->GetLevel() &&
                    bot->GetLevel() <= player->GetLevel() + BOSS_LEVEL_SPREAD)
                    callers.push_back(bot);
            if (callers.empty())
                continue;
            last = now;
            Slots const slots = { { "boss", boss->GetName() }, { "zone", AreaName(player->GetZoneId()) } };
            std::vector<time_t> times;
            for (size_t i = 0, n = Speakers(callers.size(), urand(0, 1)); i < n; ++i)
                times.push_back(now + urand(2, 12));
            times = CoaWorldChatterRules::Spread(times, 3);
            for (size_t i = 0; i < times.size(); ++i)
                Say(callers[i], "world_boss_call", slots, times[i]);
        }
    }

    void Fishing(std::vector<Player*> const& humans, time_t now)
    {
        if (!settings.fishing)
            return;
        for (Player* player : humans)
        {
            Spell* spell = player->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!spell || !spell->GetSpellInfo() || !spell->GetSpellInfo()->IsAbilityOfSkillType(SKILL_FISHING))
                continue;
            time_t& last = _fishing[player->GetGUID().GetCounter()];
            if (!Due(last, now, FISHING_SECONDS))
                continue;
            for (Player* bot : BotsNear(player, FISHING_YARDS))
                if (SayNow(bot, "fishing_idle", {}))
                {
                    last = now;
                    break;
                }
        }
    }

    struct RareCheck
    {
        Player* bot;
        bool operator()(Creature* creature) const
        {
            return creature->IsAlive() && !creature->IsInCombat() && !creature->IsPet() && !creature->IsTotem() &&
                   RareRank(creature->GetCreatureTemplate()->rank) &&
                   creature->IsWithinDistInMap(bot, RARE_SIGHT_YARDS);
        }
    };

    void Rare(std::vector<Player*> const& humans, time_t now)
    {
        if (!settings.rareSeen)
            return;
        for (Player* player : humans)
        {
            if (!OpenWorld(player))
                continue;
            time_t& last = _rare[player->GetZoneId()];
            if (!Due(last, now, RARE_SECONDS))
                continue;
            std::vector<Player*> bots = BotsNear(player, RARE_BOT_YARDS);
            if (bots.size() > 5)
                bots.resize(5);
            for (Player* bot : bots)
            {
                std::list<Creature*> rares;
                RareCheck check{ bot };
                Acore::CreatureListSearcher<RareCheck> searcher(bot, rares, check);
                Cell::VisitObjects(bot, searcher, RARE_SIGHT_YARDS);
                if (rares.empty())
                    continue;
                Creature* rare = rares.front();
                std::string place = AreaName(bot->GetAreaId());
                std::string const zone = AreaName(bot->GetZoneId());
                Slots const slots = { { "boss", rare->GetName() }, { "zone", zone },
                                      { "place", place.empty() ? zone : place } };
                if (SayNow(bot, "rare_seen", slots))
                {
                    last = now;
                    break;
                }
            }
        }
    }

    void Season(std::vector<Player*> const& humans, time_t now)
    {
        if (!settings.season)
            return;
        uint32 holiday = 0;
        for (uint32 id : SeasonHolidays())
            if (IsHolidayActive(HolidayIds(id)))
            {
                holiday = id;
                break;
            }
        if (!holiday)
            return;
        for (Player* player : humans)
        {
            if (!OpenWorld(player))
                continue;
            time_t& last = _season[player->GetZoneId()];
            if (!Due(last, now, SEASON_SECONDS))
                continue;
            Slots const slots = { { "event", SeasonName(holiday, BotsFrench()) },
                                  { "zone", AreaName(player->GetZoneId()) } };
            for (Player* bot : BotsNear(player, 0))
                if (SayNow(bot, "season_event", slots))
                {
                    last = now;
                    break;
                }
        }
    }

    void Night(std::vector<Player*> const& humans, time_t now)
    {
        if (!settings.night)
            return;
        tm local = {};
        time_t const t = now;
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        if (!NightHour(local.tm_hour))
            return;
        for (Player* player : humans)
        {
            uint32 const guildId = player->GetGuildId();
            if (!guildId)
                continue;
            time_t& last = _night[guildId];
            if (!Due(last, now, NIGHT_SECONDS) || !roll_chance_i(20))
                continue;
            std::vector<Player*> mates;
            for (auto const& [guid, bot] : RandomBots())
                if (Free(bot) && bot->GetGuildId() == guildId)
                    mates.push_back(bot);
            std::shuffle(mates.begin(), mates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            for (Player* bot : mates)
                if (SayNow(bot, "night_chat", {}))
                {
                    last = now;
                    break;
                }
        }
    }

    void OnWeather(std::vector<Player*> const& humans, uint32 zone, uint32 state, time_t now)
    {
        char const* situation = WeatherSituation(WeatherOf(state));
        if (!situation)
            return;
        time_t& last = _weather[zone];
        if (!Due(last, now, WEATHER_SECONDS))
            return;
        for (Player* player : humans)
        {
            if (player->GetZoneId() != zone || !OpenWorld(player))
                continue;
            for (Player* bot : BotsNear(player, WEATHER_YARDS))
                if (SayNow(bot, situation, {}))
                {
                    last = now;
                    return;
                }
        }
    }

    std::mutex _lock;
    std::vector<Line> _lines;
    std::vector<std::pair<uint32, uint32>> _weathers;  // zone, WeatherState
    std::unordered_map<uint32, time_t> _boss, _fishing, _rare, _season, _night, _weather;
    uint32 _timer = 0;
    uint32 _ticks = 0;
};

void LoadSettings()
{
    auto on = [](char const* name) { return sConfigMgr->GetOption<bool>(std::string(PREFIX) + name, true, false); };
    settings.bgEnd = on("BgEnd");
    settings.worldBoss = on("WorldBoss");
    settings.rareSeen = on("RareSeen");
    settings.season = on("Season");
    settings.night = on("Night");
    settings.weather = on("Weather");
    settings.fishing = on("Fishing");
}

class CoaWorldChatterWorldScript : public WorldScript
{
public:
    CoaWorldChatterWorldScript()
        : WorldScript("CoaWorldChatterWorldScript",
                      { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }
    void OnStartup() override { LoadSettings(); }
    void OnUpdate(uint32 diff) override { Chatter::Instance().Update(diff); }
};

class CoaWorldChatterBattlegroundScript : public AllBattlegroundScript
{
public:
    CoaWorldChatterBattlegroundScript()
        : AllBattlegroundScript("CoaWorldChatterBattlegroundScript", { ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END })
    {
    }
    void OnBattlegroundEnd(Battleground* bg, TeamId winner) override
    {
        Chatter::Instance().BattlegroundEnded(bg, winner);
    }
};

class CoaWorldChatterWeatherScript : public ALEScript
{
public:
    CoaWorldChatterWeatherScript() : ALEScript("CoaWorldChatterWeatherScript") {}
    void OnWeatherChange(Weather* weather, WeatherState state, float /*grade*/) override
    {
        if (weather)
            Chatter::Instance().WeatherChanged(weather->GetZone(), uint32(state));
    }
};
}  // namespace

void AddSC_coa_world_chatter()
{
    new CoaWorldChatterWorldScript();
    new CoaWorldChatterBattlegroundScript();
    new CoaWorldChatterWeatherScript();
}
