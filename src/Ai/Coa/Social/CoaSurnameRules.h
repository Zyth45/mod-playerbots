/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASURNAMERULES_H
#define PLAYERBOTS_COASURNAMERULES_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// The surname of the random bots, AiPlayerbot.CoaBotSurname:
//   0 - none ("Kegarink"),
//   1 - "Bot" ("Kegarink Bot", the default),
//   2 - a family name of AiPlayerbot.CoaBotSurnameList, the same for a bot at every start (drawn from its guid).
// Pure rules, no game object, so that they can be tested alone.

enum CoaSurnameMode : uint32_t
{
    COA_SURNAME_NONE = 0,
    COA_SURNAME_BOT = 1,
    COA_SURNAME_FAMILY = 2,
};

// Any other value reads as the default, 1.
inline uint32_t CoaSurnameModeOf(int64_t raw)
{
    return raw == COA_SURNAME_NONE || raw == COA_SURNAME_FAMILY ? static_cast<uint32_t>(raw) : COA_SURNAME_BOT;
}

// Neutral family names, used when AiPlayerbot.CoaBotSurnameList is empty. A name part takes 2 to 12 letters and
// never three times the same letter in a row (ObjectMgr::CheckPlayerName).
inline std::vector<std::string> const& CoaDefaultSurnames()
{
    static std::vector<std::string> const names = {
        "Stormhand",  "Ashvale",    "Ashford",    "Brightwood", "Ironfield",  "Greymantle", "Thornbrook", "Highwater",
        "Coldbrook",  "Ravenhill",  "Duskwalker", "Stonefield", "Amberly",    "Fairwind",   "Marshfield", "Redbrook",
        "Westbrook",  "Hollowell",  "Oakridge",   "Elmstead",   "Whitmore",   "Blackwood",  "Goldfield",  "Silverbrook",
        "Dunmore",    "Hartwell",   "Kettleby",   "Copperfield", "Moorland",  "Lakewood",   "Pinecrest",  "Rivenford",
        "Stoneholt",  "Thistle",    "Wheatley",   "Brambley",   "Hearthwell", "Applewood",  "Briarwood",  "Cindervale",
        "Dawnridge",  "Embermoor",  "Flintridge", "Frostvale",  "Glenwood",   "Hawkridge",  "Ironhart",   "Kingsley",
        "Longmeadow", "Mistvale",   "Northcott",  "Ravenwood",  "Saltmarsh",  "Shadowbrook", "Southwick", "Starfall",
        "Stormvale",  "Sunridge",   "Tallow",     "Underhill",  "Wolfcrest",  "Wyndham",    "Yarrow",     "Larkspur",
    };
    return names;
}

// A family name the realm accepts as the second part of a name: 2 to 12 ASCII letters, no letter three times in a
// row. It is written with a capital, the rest in lower case ("ashford" -> "Ashford").
inline bool CoaSurnameWord(std::string& word)
{
    if (word.size() < 2 || word.size() > 12)
        return false;
    for (size_t i = 0; i < word.size(); ++i)
    {
        char c = word[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            return false;
        if (i == 0 && c >= 'a' && c <= 'z')
            word[i] = static_cast<char>(c - 'a' + 'A');
        else if (i > 0 && c >= 'A' && c <= 'Z')
            word[i] = static_cast<char>(c - 'A' + 'a');
    }
    for (size_t i = 2; i < word.size(); ++i)
    {
        char a = static_cast<char>(word[i - 2] | 0x20), b = static_cast<char>(word[i - 1] | 0x20),
             c = static_cast<char>(word[i] | 0x20);
        if (a == b && b == c)
            return false;
    }
    return true;
}

// AiPlayerbot.CoaBotSurnameList: words separated by commas, spaces around them dropped, a word the realm would
// refuse left out, each word once. Empty (or nothing usable): the default list.
inline std::vector<std::string> CoaParseSurnameList(std::string const& text, std::vector<std::string>* rejected = nullptr)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        std::string word = text.substr(start, end - start);
        size_t const first = word.find_first_not_of(" \t\"");
        size_t const last = word.find_last_not_of(" \t\"");
        word = first == std::string::npos ? std::string() : word.substr(first, last - first + 1);
        if (!word.empty())
        {
            std::string checked = word;
            if (CoaSurnameWord(checked))
            {
                if (seen.insert(checked).second)
                    out.push_back(checked);
            }
            else if (rejected)
                rejected->push_back(word);
        }
        start = end + 1;
    }
    return out.empty() ? CoaDefaultSurnames() : out;
}

// The family name of a bot: always the same for the same guid and list.
inline std::string const& CoaSurnameFor(uint64_t guid, std::vector<std::string> const& list)
{
    // splitmix64: neighbouring guids get unrelated names, and the result is the same on every platform.
    uint64_t z = guid + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return list[z % list.size()];
}

// The first name of a random bot: its name without the surname it may carry (random bot names are one word).
inline std::string CoaBotFirstName(std::string const& name)
{
    size_t const space = name.find(' ');
    return space == std::string::npos ? name : name.substr(0, space);
}

// The name a random bot should carry in the given mode.
inline std::string CoaWantedBotName(std::string const& name, uint32_t mode, uint64_t guid,
                                    std::vector<std::string> const& list)
{
    std::string const first = CoaBotFirstName(name);
    switch (mode)
    {
        case COA_SURNAME_NONE:
            return first;
        case COA_SURNAME_FAMILY:
            return list.empty() ? first : first + " " + CoaSurnameFor(guid, list);
        default:
            return first + " Bot";
    }
}

// A character of the realm, as ApplyCoaBotSurnames reads it: only random bots are renamed, every other one (a
// real player, a player's alt) only holds its name.
struct CoaNameRow
{
    uint64_t guid;
    std::string name;
    bool randomBot;
};

struct CoaRename
{
    uint64_t guid;
    std::string from;
    std::string to;
};

// Names compare without case: "Kegarink ashford" and "Kegarink Ashford" are one name to a player typing it.
inline std::string CoaNameKey(std::string const& name)
{
    std::string key = name;
    for (char& c : key)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return key;
}

// The renames that give every random bot the name of the mode. A name stays unique on the realm: a bot whose wanted
// name is held (by a real player, an alt or another bot, renamed or not) keeps its own, and so does a bot whose
// wanted name the realm refuses (accepted(name) false). Only names change, never a guid.
template <class Accepted>
std::vector<CoaRename> CoaPlanBotRenames(std::vector<CoaNameRow> const& rows, uint32_t mode,
                                         std::vector<std::string> const& list, Accepted accepted,
                                         uint32_t* kept = nullptr)
{
    std::map<std::string, uint32_t> held;
    for (CoaNameRow const& row : rows)
        ++held[CoaNameKey(row.name)];

    std::vector<CoaRename> out;
    uint32_t keptCount = 0;
    for (CoaNameRow const& row : rows)
    {
        if (!row.randomBot)
            continue;
        std::string const wanted = CoaWantedBotName(row.name, mode, row.guid, list);
        if (wanted == row.name)
            continue;
        // A change of case only ("Kegarink bot" -> "Kegarink Bot") frees nothing and takes nothing.
        std::string const key = CoaNameKey(wanted);
        bool const sameKey = key == CoaNameKey(row.name);
        if ((!sameKey && held.count(key)) || !accepted(wanted))
        {
            ++keptCount;
            continue;
        }
        if (!sameKey)
        {
            auto it = held.find(CoaNameKey(row.name));
            if (--it->second == 0)
                held.erase(it);
            ++held[key];
        }
        out.push_back({row.guid, row.name, wanted});
    }
    if (kept)
        *kept = keptCount;
    return out;
}

#endif
