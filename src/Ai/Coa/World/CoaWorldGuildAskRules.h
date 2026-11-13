/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: a real player asks aloud for a guild (no game type, tested in tests/coa_world_guild_ask_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDGUILDASKRULES_H
#define PLAYERBOTS_COAWORLDGUILDASKRULES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace CoaWorldGuildAskRules
{
// Lower case, every other character than a letter or a digit a space, one space between words and one at each end
// (" any guild "), so a phrase is found as " a phrase ".
inline std::string Words(std::string const& text, size_t* count = nullptr)
{
    std::string out = " ";
    size_t words = 0;
    bool inWord = false;
    for (unsigned char c : text)
    {
        if (c < 128 && std::isalnum(c))
        {
            out += char(std::tolower(c));
            if (!inWord)
                ++words;
            inWord = true;
        }
        else if (inWord)
        {
            out += ' ';
            inWord = false;
        }
    }
    if (inWord)
        out += ' ';
    if (count)
        *count = words;
    return out;
}

inline bool Has(std::string const& words, char const* phrase) { return words.find(phrase) != std::string::npos; }

inline bool HasAny(std::string const& words, std::vector<char const*> const& phrases)
{
    return std::any_of(phrases.begin(), phrases.end(), [&words](char const* p) { return Has(words, p); });
}

// A player who has no guild asks for one, in English or French: "LF guild", "any guild?", "anyone have a guild I
// could join?", "guild request", "guild invite pls", "cherche une guilde", "guilde ?", "quelqu'un a une guilde ?".
// Not a guild's own call ("<Name> is recruiting"), not a player talking about their guild ("join my guild"), not a
// question about a guild ("what guild is that?").
inline bool AsksForGuild(std::string const& text)
{
    if (text.find('<') != std::string::npos && text.find('>') != std::string::npos)
        return false;  // a guild's name in brackets: a recruiting call
    size_t count = 0;
    std::string const w = Words(text, &count);
    if (!count || count > 20)
        return false;
    bool const guildWord = HasAny(w, { " guild ", " guilds ", " guilde ", " guildes ", " ginv ", " ginvite ", " lfguild " });
    if (!guildWord)
        return false;
    bool const question = text.find('?') != std::string::npos;

    // Their own guild, or a call for members.
    if (HasAny(w, { " my guild ", " our guild ", " my new guild ", " ma guilde ", " notre guilde ", " ma nouvelle guilde ",
                    " mon guilde ", " we recruit ", " we are recruiting ", " recrutons ", " on recrute " }))
        return false;
    if (!question && HasAny(w, { " is recruiting ", " now recruiting ", " recrute ", " recrutent " }))
        return false;

    if (HasAny(w, { " ginv ", " ginvite ", " lfguild ", " lf guild ", " lfg guild ", " lf a guild ", " lf new guild ",
                    " looking for a guild ", " looking for guild ", " looking for a new guild ", " looking for new guild ",
                    " looking for a good guild ", " looking for an active guild ", " any guild ", " any guilds ",
                    " anyone have a guild ", " anyone got a guild ", " anybody have a guild ", " anybody got a guild ",
                    " any1 have a guild ", " someone have a guild ", " anyone in a guild ", " need a guild ",
                    " need guild ", " want a guild ", " want guild ", " guild request ", " guild invite ", " guild inv ",
                    " guild pls ", " guild plz ", " guild please ", " guilds recruiting ", " recruiting guild ",
                    " recruiting guilds ", " a guild to join ", " guild to join ", " guild i could join ",
                    " guild i can join ",
                    " cherche une guilde ", " cherche guilde ", " recherche une guilde ", " recherche guilde ",
                    " cherche une nouvelle guilde ", " quelqu un a une guilde ", " qqn a une guilde ",
                    " quelqu un aurait une guilde ", " une guilde svp ", " une guilde stp ", " guilde svp ",
                    " guilde stp ", " inv guilde ", " invit guilde ", " invite guilde ", " invitation guilde ",
                    " invitation de guilde ", " invitation en guilde ", " inviter en guilde ", " besoin d une guilde ",
                    " besoin de guilde ", " guildes qui recrutent ", " guilde qui recrute ", " une guilde a rejoindre ",
                    " veux une guilde ", " voudrais une guilde ", " aimerais une guilde " }))
        return true;

    // "can I join a guild", "rejoindre une guilde", "invite me to your guild", "m'inviter dans une guilde".
    if (HasAny(w, { " join ", " rejoindre ", " rejoins ", " invite me ", " inv me ", " m inviter ", " m invite ",
                    " minviter " }))
        return true;

    // A short question: "guild?", "a guild?", "une guilde ?", not "which guild?".
    if (question && count <= 4 &&
        !HasAny(w, { " what ", " which ", " who ", " whose ", " quelle ", " quel ", " qui ", " laquelle " }))
        return true;
    return false;
}

// One ask per player at a time: a new one is heard when none is pending and the last one is older than the rest.
inline bool MayAnswer(bool pending, bool inGuild, bool invited, time_t last, time_t now, uint32_t restSeconds)
{
    if (pending || inGuild || invited)
        return false;
    return !last || now - last >= time_t(restSeconds);
}
}  // namespace CoaWorldGuildAskRules

#endif
