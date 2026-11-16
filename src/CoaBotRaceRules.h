/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_BOT_RACE_RULES_H
#define COA_BOT_RACE_RULES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// How random bots use each race (CoA Bots 3.1.4). A server that adds races describes them in its playerbots.conf, one
// line per race, without touching the module's code:
//   AiPlayerbot.BotRace.85 = "bots=1 gender=female side=horde look=simple names=orc classes=1,2,5"
// Every field is optional; a field left out keeps the module's own rule, so a server without these lines behaves
// exactly as before. A line that does not parse is ignored as a whole (a log line says why). Pure rules, tested alone.
namespace CoaBotRace
{
enum class Gender : std::uint8_t
{
    Unset,   // the module's list, then the core's male-only races, then a coin
    Any,     // a coin, even for a race a list makes single-gender
    Male,
    Female
};

enum class Side : std::uint8_t
{
    Unset,  // base races by id, added races from ChrRaces.dbc (TeamID)
    Alliance,
    Horde
};

enum class Look : std::uint8_t
{
    Full,   // any face, hair and facial hair of the race in CharSections.dbc
    Simple  // the first variant only (section type 0) and no facial hair: for races whose other variants lack textures
};

struct Profile
{
    std::int8_t bots = -1;              // -1: base races yes, added races with CoACustomRaces.BotsEnable; 0: never; 1: yes
    Gender gender = Gender::Unset;
    Side side = Side::Unset;
    Look look = Look::Full;
    std::uint8_t namesRace = 0;         // 0: the race's own name pool; else the pool of that base race (generic past it)
    std::vector<std::uint8_t> classes;  // empty: every class the race has in playercreateinfo
};

constexpr std::uint8_t MAX_RACE = 255;

inline bool IsBaseRace(std::uint32_t race) { return race >= 1 && race <= 11 && race != 9; }

// The side of a base race (1 to 11): the same as the core's Alliance race mask.
inline bool BaseRaceIsAlliance(std::uint32_t race)
{
    return race == 1 || race == 3 || race == 4 || race == 7 || race == 11;
}

// The module's own gender list, unchanged from 3.1.3 (CoA Custom races with one usable model, client tests of 15/10).
inline Gender DefaultGender(std::uint32_t race)
{
    if (race == 57)
        return Gender::Female;
    if (race == 15 || race == 17 || race == 18 || (race >= 22 && race <= 26) || race == 32 || race == 50 ||
        race == 53 || race == 68 || race == 69 || race == 83 || race == 97)
        return Gender::Male;
    return Gender::Unset;
}

// The gender a new bot of the race gets: the server's line, else the module's list, else the core's male-only races.
// Unset/Any: a coin.
inline Gender ResolveGender(Profile const& profile, std::uint32_t race, bool coreMaleOnly)
{
    if (profile.gender != Gender::Unset)
        return profile.gender;
    if (Gender const def = DefaultGender(race); def != Gender::Unset)
        return def;
    return coreMaleOnly ? Gender::Male : Gender::Any;
}

// Random bots may use the race. excluded: AiPlayerbot.ExcludedBotRaces and CrashingBotRaces, which always win.
inline bool Allowed(std::uint32_t race, Profile const& profile, bool customRacesEnabled,
                    std::vector<std::uint32_t> const& excluded)
{
    if (std::find(excluded.begin(), excluded.end(), race) != excluded.end())
        return false;
    if (profile.bots >= 0)
        return profile.bots == 1;
    return IsBaseRace(race) || customRacesEnabled;
}

// The class may be given to a bot of the race (the core's playercreateinfo is checked as well, by the caller).
inline bool ClassAllowed(Profile const& profile, std::uint8_t cls)
{
    return profile.classes.empty() || std::find(profile.classes.begin(), profile.classes.end(), cls) != profile.classes.end();
}

// The side: the server's line for an added race, else the caller's (base races by id, added races from ChrRaces).
// A base race keeps its side: a line cannot move Humans to the Horde.
inline bool IsAllianceWith(Profile const& profile, std::uint32_t race, bool sideFromCore)
{
    if (!IsBaseRace(race) && profile.side != Side::Unset)
        return profile.side == Side::Alliance;
    return sideFromCore;
}

// A CharSections entry may be drawn: for a Simple look, only its type 0 (face, hair) and no facial hair at all.
// genType: 0 skin, 1 face, 2 facial hair, 3 hair (SECTION_TYPE_*).
inline bool KeepSection(Look look, std::uint8_t genType, std::uint8_t type)
{
    if (look == Look::Full)
        return true;
    if (genType == 2)
        return false;
    return genType == 0 || type == 0;
}

// The name pool of a base race named in a line.
inline std::uint8_t NamesRaceOf(std::string const& word)
{
    static char const* const pools[] = {"",     "human",  "orc",   "dwarf",    "nightelf", "undead",
                                        "tauren", "gnome", "troll", "generic", "bloodelf", "draenei"};
    for (std::uint8_t i = 1; i < sizeof(pools) / sizeof(pools[0]); ++i)
        if (word == pools[i])
            return i;
    return 0;
}

inline bool AllDigits(std::string const& text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}

inline std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

// Reads one line ("bots=1 gender=female side=horde look=simple names=orc classes=1,2,5"; spaces or ';' between
// fields, case ignored). False with a reason when a field or a value is unknown: the caller then ignores the whole
// line, so that a typo never gives half a race.
inline bool Parse(std::string const& text, Profile& out, std::string& error)
{
    Profile profile;
    std::string normalized = Lower(text);
    std::replace(normalized.begin(), normalized.end(), ';', ' ');
    std::istringstream fields(normalized);
    for (std::string field; fields >> field;)
    {
        std::size_t const eq = field.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == field.size())
        {
            error = "\"" + field + "\" is not field=value";
            return false;
        }
        std::string const key = field.substr(0, eq);
        std::string const value = field.substr(eq + 1);
        if (key == "bots")
        {
            if (value != "0" && value != "1")
                return error = "bots must be 0 or 1", false;
            profile.bots = std::int8_t(value == "1");
        }
        else if (key == "gender")
        {
            if (value == "any")
                profile.gender = Gender::Any;
            else if (value == "male")
                profile.gender = Gender::Male;
            else if (value == "female")
                profile.gender = Gender::Female;
            else
                return error = "gender must be any, male or female", false;
        }
        else if (key == "side")
        {
            if (value == "alliance")
                profile.side = Side::Alliance;
            else if (value == "horde")
                profile.side = Side::Horde;
            else if (value == "dbc")
                profile.side = Side::Unset;
            else
                return error = "side must be alliance, horde or dbc", false;
        }
        else if (key == "look")
        {
            if (value == "full")
                profile.look = Look::Full;
            else if (value == "simple")
                profile.look = Look::Simple;
            else
                return error = "look must be full or simple", false;
        }
        else if (key == "names")
        {
            profile.namesRace = NamesRaceOf(value);
            if (!profile.namesRace)
                return error = "names must be human, orc, dwarf, nightelf, undead, tauren, gnome, troll, bloodelf, "
                               "draenei or generic",
                       false;
        }
        else if (key == "classes")
        {
            std::istringstream ids(value);
            for (std::string id; std::getline(ids, id, ',');)
            {
                if (id.empty() || id.size() > 2 || !AllDigits(id) || std::stoi(id) < 1 ||
                    std::stoi(id) > 31)
                    return error = "classes must be class ids from 1 to 31 separated by commas", false;
                profile.classes.push_back(std::uint8_t(std::stoi(id)));
            }
        }
        else
            return error = "unknown field \"" + key + "\"", false;
    }
    out = std::move(profile);
    return true;
}

// The race id of a key "AiPlayerbot.BotRace.<id>"; 0 when the suffix is not an id from 1 to 255.
inline std::uint32_t RaceOfKey(std::string const& key, std::string const& prefix)
{
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
        return 0;
    std::string const id = key.substr(prefix.size());
    if (id.empty() || id.size() > 3 || !AllDigits(id))
        return 0;
    std::uint32_t const race = std::uint32_t(std::stoul(id));
    return race >= 1 && race <= MAX_RACE ? race : 0;
}

// The race ids of a key: "AiPlayerbot.BotRace.85", or "AiPlayerbot.BotRace.20/29" for a race with one id per side
// (the same notation as the custom races' list). Empty when any id is not from 1 to 255.
inline std::vector<std::uint32_t> RacesOfKey(std::string const& key, std::string const& prefix)
{
    std::vector<std::uint32_t> races;
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
        return races;
    std::string const ids = key.substr(prefix.size());
    std::size_t start = 0;
    while (true)
    {
        std::size_t const slash = ids.find('/', start);
        std::uint32_t const race = RaceOfKey(prefix + ids.substr(start, slash - start), prefix);
        if (!race || races.size() >= 8)
            return {};
        if (std::find(races.begin(), races.end(), race) == races.end())
            races.push_back(race);
        if (slash == std::string::npos)
            return races;
        start = slash + 1;
    }
}
}  // namespace CoaBotRace

#endif
