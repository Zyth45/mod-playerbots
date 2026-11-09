/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAWORLDFILERULES_H
#define PLAYERBOTS_COAWORLDFILERULES_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// world.json for the CoA Dashboard (D:\CoA-Dashboard\docs\LIAISON-3.0.md §3, §6): population and health, built only
// while the dashboard watches it. Pure rules: the activity of a bot, the watch request, the JSON text.
namespace CoaWorldFile
{
enum class Activity : uint8_t
{
    Quest,
    Grind,
    City,
    Travel,
    Fish,
    Pvp,
    Dungeon,
    Idle,
    Gather,  // a gathering trip (herbs, ore): added last, the older keys keep their place
    Count,
};

inline char const* ActivityName(Activity a)
{
    static char const* const names[] = {"quest", "grind", "city", "travel", "fish", "pvp", "dungeon", "idle", "gather"};
    return a < Activity::Count ? names[uint8_t(a)] : "idle";
}

// What a bot does, from what is known of it; the first that applies wins.
struct BotState
{
    bool battleground = false;  // a battleground or an arena
    bool dungeon = false;       // another instanced map
    bool fishing = false;
    bool gathering = false;  // on its way to a node (CoA Live Immersion)
    bool flying = false;        // on a taxi
    bool inCapital = false;
    // The "new rpg" status, by name: idle, rest, grind, camp, wander_random, wander_npc, quest, flight, pvp, city.
    std::string rpg;
};

inline Activity Classify(BotState const& s)
{
    if (s.battleground || s.rpg == "pvp")
        return Activity::Pvp;
    if (s.dungeon)
        return Activity::Dungeon;
    if (s.fishing)
        return Activity::Fish;
    if (s.gathering)
        return Activity::Gather;
    if (s.flying || s.rpg == "flight" || s.rpg == "camp")
        return Activity::Travel;
    if (s.rpg == "city" || (s.inCapital && (s.rpg == "wander_npc" || s.rpg == "rest" || s.rpg == "idle")))
        return Activity::City;
    if (s.rpg == "quest")
        return Activity::Quest;
    if (s.rpg == "grind" || s.rpg == "wander_random" || s.rpg == "wander_npc")
        return Activity::Grind;
    return Activity::Idle;
}

// squidbot/watch.json: {"at": 1762600000, "topics": ["world", "economy"]}. True when "world" is asked and the
// request is less than 90 s old (by its own "at", else by the file's date).
inline bool Watched(std::string const& text, int64_t fileTime, int64_t now)
{
    if (text.find("\"world\"") == std::string::npos)
        return false;
    int64_t at = fileTime;
    size_t const key = text.find("\"at\"");
    if (key != std::string::npos)
    {
        size_t i = text.find(':', key);
        if (i != std::string::npos)
        {
            ++i;
            while (i < text.size() && text[i] == ' ')
                ++i;
            int64_t v = 0;
            bool any = false;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            {
                v = v * 10 + (text[i] - '0');
                ++i;
                any = true;
            }
            if (any)
                at = v;
        }
    }
    return now >= at && now - at < 90;
}

// A JSON string: quotes, backslashes and control characters escaped.
inline std::string Quote(std::string const& s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\', out += c;
        else if (static_cast<unsigned char>(c) < 0x20)
            out += ' ';
        else
            out += c;
    }
    return out + "\"";
}

struct ZoneCount
{
    uint32_t map = 0;
    uint32_t zone = 0;
    uint32_t bots = 0;
    bool capital = false;
    uint32_t alliance = 0;  // the random bots of each faction
    uint32_t horde = 0;
};

// "everyone": every character online, by kind: real players, alts and other bots of players, random bots.
enum class Kind : uint8_t
{
    Player,
    Alt,
    Bot,
};

struct Mix
{
    uint32_t players = 0;
    uint32_t alts = 0;
    uint32_t bots = 0;

    void Add(Kind k) { ++(k == Kind::Player ? players : k == Kind::Alt ? alts : bots); }
    uint32_t Total() const { return players + alts + bots; }
};

// Where a character is, for "everyone": the first that applies.
enum class Place : uint8_t
{
    Battleground,
    Arena,
    Raid,
    Dungeon,
    Capital,
    World,
    Count,
};

inline char const* PlaceName(Place p)
{
    static char const* const names[] = {"battleground", "arena", "raid", "dungeon", "capital", "world"};
    return p < Place::Count ? names[uint8_t(p)] : "world";
}

inline Place PlaceOf(bool battleground, bool arena, bool raid, bool dungeon, bool capital)
{
    if (arena)
        return Place::Arena;
    if (battleground)
        return Place::Battleground;
    if (raid)
        return Place::Raid;
    if (dungeon)
        return Place::Dungeon;
    return capital ? Place::Capital : Place::World;
}

struct EveryoneZone
{
    uint32_t map = 0;
    uint32_t zone = 0;
    bool capital = false;
    Mix mix;
};

struct Everyone
{
    Mix total;
    Mix places[uint8_t(Place::Count)];
    Mix alliance;
    Mix horde;
    std::vector<EveryoneZone> zones;
};

inline std::string MixJson(Mix const& m)
{
    return "{\"players\":" + std::to_string(m.players) + ",\"alts\":" + std::to_string(m.alts) + ",\"bots\":" +
           std::to_string(m.bots) + "}";
}

// The "everyone" object: totals, places, factions, the busiest zones (all characters together).
inline std::string EveryoneJson(Everyone e, size_t maxZones = 40)
{
    std::sort(e.zones.begin(), e.zones.end(), [](EveryoneZone const& a, EveryoneZone const& b)
              { return a.mix.Total() != b.mix.Total() ? a.mix.Total() > b.mix.Total() : a.zone < b.zone; });
    if (e.zones.size() > maxZones)
        e.zones.resize(maxZones);
    std::string out = "{\"total\":" + MixJson(e.total) + ",\"places\":{";
    for (uint8_t p = 0; p < uint8_t(Place::Count); ++p)
        out += std::string(p ? "," : "") + Quote(PlaceName(Place(p))) + ":" + MixJson(e.places[p]);
    out += "},\"factions\":{\"alliance\":" + MixJson(e.alliance) + ",\"horde\":" + MixJson(e.horde) + "},\"zones\":[";
    for (size_t i = 0; i < e.zones.size(); ++i)
        out += std::string(i ? "," : "") + "{\"map\":" + std::to_string(e.zones[i].map) + ",\"zone\":" +
               std::to_string(e.zones[i].zone) + ",\"capital\":" + (e.zones[i].capital ? "true" : "false") + "," +
               MixJson(e.zones[i].mix).substr(1);
    return out + "]}";
}

struct Snapshot
{
    uint32_t online = 0;   // random bots in the world
    uint32_t players = 0;  // real players
    std::vector<ZoneCount> zones;
    uint32_t activities[uint8_t(Activity::Count)] = {};
    int32_t surnameMode = -1;  // -1: null
    uint32_t surnameListSize = 0;
    std::map<std::string, uint32_t> surnames;  // mode 2: family name -> bots online
    bool soulsOn = false;  // false: null
    uint32_t souls[5] = {};  // lazy, quester, wanderer, trader, pvp
    int64_t tickAvgMs = -1;  // -1: null
    int64_t tickMaxMs = -1;
    int64_t tickP95Ms = -1;
    Everyone everyone;  // every character online ("everyone")
};

inline std::string Num(int64_t v) { return v < 0 ? "null" : std::to_string(v); }

// The "data" object (§3, with the §6 nulls: no souls yet, no perf monitor, no stuck detector).
inline std::string Data(Snapshot s, size_t maxZones = 40, size_t maxSurnames = 10)
{
    std::sort(s.zones.begin(), s.zones.end(), [](ZoneCount const& a, ZoneCount const& b)
              { return a.bots != b.bots ? a.bots > b.bots : a.zone < b.zone; });
    if (s.zones.size() > maxZones)
        s.zones.resize(maxZones);

    std::string out = "{\"online\":" + std::to_string(s.online) + ",\"players\":" + std::to_string(s.players);
    out += ",\"zones\":[";
    for (size_t i = 0; i < s.zones.size(); ++i)
        out += std::string(i ? "," : "") + "{\"map\":" + std::to_string(s.zones[i].map) + ",\"zone\":" +
               std::to_string(s.zones[i].zone) + ",\"bots\":" + std::to_string(s.zones[i].bots) + ",\"capital\":" +
               (s.zones[i].capital ? "true" : "false") + ",\"faction\":{\"alliance\":" +
               std::to_string(s.zones[i].alliance) + ",\"horde\":" + std::to_string(s.zones[i].horde) + "}}";
    out += "],\"activities\":{";
    for (uint8_t a = 0; a < uint8_t(Activity::Count); ++a)
        out += std::string(a ? "," : "") + Quote(ActivityName(Activity(a))) + ":" + std::to_string(s.activities[a]);
    out += "},\"souls\":";
    if (!s.soulsOn)
        out += "null";
    else
    {
        static char const* const names[] = {"lazy", "quester", "wanderer", "trader", "pvp"};
        out += "{";
        for (int i = 0; i < 5; ++i)
            out += std::string(i ? "," : "") + Quote(names[i]) + ":" + std::to_string(s.souls[i]);
        out += "}";
    }
    out += ",\"surname\":";
    if (s.surnameMode < 0)
        out += "null";
    else
    {
        std::vector<std::pair<std::string, uint32_t>> top(s.surnames.begin(), s.surnames.end());
        std::sort(top.begin(), top.end(), [](auto const& a, auto const& b)
                  { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        if (top.size() > maxSurnames)
            top.resize(maxSurnames);
        out += "{\"mode\":" + std::to_string(s.surnameMode) + ",\"list_size\":" + std::to_string(s.surnameListSize) +
               ",\"top\":[";
        for (size_t i = 0; i < top.size(); ++i)
            out += std::string(i ? "," : "") + "{\"name\":" + Quote(top[i].first) + ",\"bots\":" +
                   std::to_string(top[i].second) + "}";
        out += "]}";
    }
    out += ",\"everyone\":" + EveryoneJson(s.everyone);
    out += ",\"health\":{\"tick_ms\":{\"avg\":" + Num(s.tickAvgMs) + ",\"p95\":" + Num(s.tickP95Ms) + ",\"max\":" + Num(s.tickMaxMs) +
           "},\"bots_us_per_tick\":null,\"by_strategy_us\":null,\"stuck\":null}}";
    return out;
}

// The envelope of every squidbot file (§3, §6).
inline std::string Envelope(int64_t at, uint32_t every, int64_t buildUs, int64_t serverStart, std::string const& data)
{
    return "{\"schema\":\"squidbot.world/1\",\"at\":" + std::to_string(at) + ",\"every\":" + std::to_string(every) +
           ",\"build_us\":" + std::to_string(buildUs) + ",\"server_start\":" + std::to_string(serverStart) +
           ",\"module\":null,\"data\":" + data + "}";
}
}  // namespace CoaWorldFile

#endif
