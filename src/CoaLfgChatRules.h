/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LFG_CHAT_RULES_H
#define COA_LFG_CHAT_RULES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// The "lfg bot heal" requests heard in chat (CoaSpecialization.cpp, CoaLfgHeard), apart from the game objects, so
// that tests/coa_lfg_chat_rules.cpp can check them. A request names roles, and since 2.9.5 CoA classes by their
// full name ("lfg bot tinker heal", "lfm bot witch doctor"): no short forms, which chat words would trigger.

// Same values as CoaRole: Dps, Tank, Heal.
constexpr std::uint8_t CoaLfgRoleDps = 0;
constexpr std::uint8_t CoaLfgRoleTank = 1;
constexpr std::uint8_t CoaLfgRoleHeal = 2;
constexpr std::uint8_t CoaLfgAnyRole = 0xFF;

constexpr std::uint8_t CoaLfgFirstClass = 12;
constexpr char const* CoaLfgClassNames[] = {
    "barbarian", "witch doctor", "felsworn", "witch hunter", "stormbringer", "knight of xoroth", "guardian",
    "templar", "bloodmage", "ranger", "chronomancer", "necromancer", "pyromancer", "cultist", "starcaller",
    "sun cleric", "tinker", "venomancer", "reaper", "primalist", "runemaster"};

// One thing asked for: a role (classId 0), a class in a role, or a class in any role it plays (role CoaLfgAnyRole).
struct CoaLfgAsk
{
    std::uint8_t role;
    std::uint8_t classId;

    bool operator==(CoaLfgAsk const& other) const { return role == other.role && classId == other.classId; }
};

struct CoaLfgRequest
{
    bool lfg = false;
    bool botWord = false;
    std::vector<CoaLfgAsk> asks;  // in the order written, each once
};

inline std::vector<std::string> CoaLfgWords(std::string const& message)
{
    std::vector<std::string> words;
    std::string word;
    for (char c : message + " ")
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            word += char(std::tolower(static_cast<unsigned char>(c)));
        else if (!word.empty())
        {
            words.push_back(word);
            word.clear();
        }
    }
    return words;
}

inline std::uint8_t CoaLfgRoleWord(std::string const& w)
{
    if (w == "heal" || w == "heals" || w == "healer" || w == "healers" || w == "healing")
        return CoaLfgRoleHeal;
    if (w == "tank" || w == "tanks")
        return CoaLfgRoleTank;
    if (w == "dps" || w == "damage" || w == "dd")
        return CoaLfgRoleDps;
    return CoaLfgAnyRole;
}

// The class whose full name starts at words[at] (plural "s" on its last word accepted), 0 for none; length gets
// the number of words it takes.
inline std::uint8_t CoaLfgClassAt(std::vector<std::string> const& words, std::size_t at, std::size_t& length)
{
    std::uint8_t best = 0;
    length = 0;
    for (std::size_t i = 0; i < std::size(CoaLfgClassNames); ++i)
    {
        std::vector<std::string> const name = CoaLfgWords(CoaLfgClassNames[i]);
        if (at + name.size() > words.size() || name.size() <= length)
            continue;
        bool same = true;
        for (std::size_t k = 0; k < name.size() && same; ++k)
        {
            std::string const& w = words[at + k];
            same = w == name[k] || (k + 1 == name.size() && w == name[k] + "s");
        }
        if (same)
        {
            best = std::uint8_t(CoaLfgFirstClass + i);
            length = name.size();
        }
    }
    return best;
}

// A class takes the role written right after it ("tinker heal"), else the one right before it ("heal tinker")
// when that role is not already taken by the class after it; a role left alone is a plain role request.
inline CoaLfgRequest CoaParseLfgRequest(std::string const& message)
{
    std::vector<std::string> const words = CoaLfgWords(message);
    struct Token
    {
        std::uint8_t role;
        std::uint8_t classId;
    };
    CoaLfgRequest request;
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < words.size();)
    {
        std::string const& w = words[i];
        std::size_t length = 0;
        if (w == "lfg" || w == "lfm" || w == "lf")
            request.lfg = true;
        else if (w == "bot" || w == "bots")
            request.botWord = true;
        else if (std::uint8_t const role = CoaLfgRoleWord(w); role != CoaLfgAnyRole)
            tokens.push_back({role, 0});
        else if (std::uint8_t const classId = CoaLfgClassAt(words, i, length))
        {
            tokens.push_back({CoaLfgAnyRole, classId});
            i += length;
            continue;
        }
        else
            tokens.push_back({CoaLfgAnyRole, 0});
        ++i;
    }

    std::vector<bool> used(tokens.size(), false);
    std::vector<CoaLfgAsk> asks(tokens.size(), CoaLfgAsk{CoaLfgAnyRole, 0});
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (!tokens[i].classId)
            continue;
        asks[i] = {CoaLfgAnyRole, tokens[i].classId};
        used[i] = true;
        if (i + 1 < tokens.size() && !tokens[i + 1].classId && tokens[i + 1].role != CoaLfgAnyRole && !used[i + 1])
        {
            asks[i].role = tokens[i + 1].role;
            used[i + 1] = true;
        }
        else if (i > 0 && !tokens[i - 1].classId && tokens[i - 1].role != CoaLfgAnyRole && !used[i - 1])
        {
            asks[i].role = tokens[i - 1].role;
            used[i - 1] = true;
        }
    }
    for (std::size_t i = 0; i < tokens.size(); ++i)
        if (!used[i] && tokens[i].role != CoaLfgAnyRole)
            asks[i] = {tokens[i].role, 0};

    for (CoaLfgAsk const& ask : asks)
        if ((ask.classId || ask.role != CoaLfgAnyRole) &&
            std::find(request.asks.begin(), request.asks.end(), ask) == request.asks.end())
            request.asks.push_back(ask);
    return request;
}

// The asks to serve; empty when the message is no request. "lfg bot" alone asks for every role.
inline std::vector<CoaLfgAsk> CoaLfgAsks(CoaLfgRequest const& request, bool requireBotWord)
{
    if (!request.lfg || (requireBotWord && !request.botWord))
        return {};
    if (request.asks.empty() && request.botWord)
        return {{CoaLfgRoleTank, 0}, {CoaLfgRoleHeal, 0}, {CoaLfgRoleDps, 0}};
    return request.asks;
}

// A class request is answered by up to two bots of that class, the best fitting free ones, so the player can pick.
constexpr std::uint32_t CoaLfgClassOffers = 2;

// Whether an account may ask for this class now (AiPlayerbot.CoaLfgClassCooldown, per account and class, 0: no wait);
// lastAsked 0: never.
inline bool CoaLfgClassAskAllowed(std::time_t lastAsked, std::time_t now, std::uint32_t cooldownSeconds)
{
    return !cooldownSeconds || !lastAsked || now - lastAsked >= std::time_t(cooldownSeconds);
}

// The wait line, matched by the SquidBots addon ("^You can ask for a (.+) again in (%d+) s"): keep it as is.
inline std::string CoaLfgClassWaitText(std::string const& className, std::uint32_t seconds)
{
    return "You can ask for a " + className + " again in " + std::to_string(seconds) +
           " s: your request is answered by role.";
}

#endif
