/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COACHANASKRULES_H
#define PLAYERBOTS_COACHANASKRULES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// AiPlayerbot.CoaLive.ChannelQuestions.Enable: a real player's open question in a public channel (world, newcomers,
// zone, LFG) gets one bot's answer, by situation: chan_class ("what class is the best?") or chan_question (any other
// open question). A guild ask is CoA World's (CoaWorldGuildAskRules): every message with a guild word is left to it.
// Without the game objects, for the unit test.
namespace CoaChanAsk
{
enum class Topic : uint8_t
{
    None = 0,
    Question,  // chan_question
    Class,     // chan_class
};

// Lower case, one space between words and one at each end (" any class "), so a phrase is found as " a phrase ".
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

inline bool HasAny(std::string const& words, std::vector<char const*> const& phrases)
{
    return std::any_of(phrases.begin(), phrases.end(),
                       [&words](char const* p) { return words.find(p) != std::string::npos; });
}

inline bool StartsWithAny(std::string const& words, std::vector<char const*> const& phrases)
{
    return std::any_of(phrases.begin(), phrases.end(), [&words](char const* p) { return words.rfind(p, 0) == 0; });
}

inline Topic Classify(std::string const& text)
{
    // A command, an item or spell link, a guild's call in brackets: not a question to the channel.
    size_t const first = text.find_first_not_of(' ');
    if (first == std::string::npos || text[first] == '.' || text[first] == '!' || text.find("|H") != std::string::npos ||
        (text.find('<') != std::string::npos && text.find('>') != std::string::npos))
        return Topic::None;
    size_t count = 0;
    std::string const w = Words(text, &count);
    if (count < 2 || count > 25)
        return Topic::None;

    // Asked elsewhere: a guild (CoA World), a group or a trade (their own channels and bots), bots themselves, and the
    // advances no bot answers.
    if (HasAny(w, {" guild ", " guilds ", " guilde ", " guildes ", " ginv ", " ginvite ", " lfguild "}))
        return Topic::None;
    if (HasAny(w, {" wts ", " wtb ", " wtt ", " lfg ", " lfm ", " lf ", " lf1m ", " lf2m ", " lf3m ", " lf4m ",
                   " need tank ", " need heal ", " need healer ", " need dps ", " inv ", " invite ", " price ",
                   " how much ", " buy ", " buying ", " sell ", " selling ", " carry ", " boost ", " combien ",
                   " achete ", " vend ", " vends ", " cherche groupe ", " cherche tank ", " cherche heal "}))
        return Topic::None;
    if (HasAny(w, {" bot ", " bots ", " robot ", " robots ", " npc ", " npcs ", " pnj "}))
        return Topic::None;
    if (HasAny(w, {" gf ", " girlfriend ", " bf ", " boyfriend ", " erp ", " cute ", " single ", " date ", " dating ",
                   " married ", " sexy ", " nude ", " nudes ", " dm me ", " copine ", " copain ", " celibataire ",
                   " mignonne ", " mignon ", " cherche une fille ", " cherche un mec "}))
        return Topic::None;

    bool const asked = text.find('?') != std::string::npos ||
                       StartsWithAny(w, {" what ", " which ", " who ", " how ", " where ", " why ", " when ", " is ",
                                         " are ", " does ", " do ", " can ", " should ", " anyone ", " anybody ",
                                         " any ", " quel ", " quelle ", " quels ", " quelles ", " qui ", " comment ",
                                         " ou ", " pourquoi ", " quand ", " est ce ", " quelqu un ", " qqn ", " y a "});
    if (!asked)
        return Topic::None;

    if (HasAny(w, {" best class ", " which class ", " what class ", " strongest class ", " op class ", " class is best ",
                   " class is the best ", " class should i ", " class to play ", " class to level ", " class to pick ",
                   " good class ", " fun class ", " easiest class ", " best spec ", " what should i play ",
                   " what to play ", " meilleure classe ", " meilleur classe ", " quelle classe ", " classe a jouer ",
                   " classe la plus ", " classe pour monter ", " classe pour leveler ", " bonne classe ",
                   " classe facile ", " que jouer ", " quoi jouer "}))
        return Topic::Class;
    return Topic::Question;
}

// One answer per player in this many seconds, whatever the channel and the bot. 5 minutes left a player's second
// question unanswered: a player asks again a minute later, and silence reads as "it does not work".
constexpr uint32_t PLAYER_CD_S = 45;

// One answer per channel in this many seconds, whatever the player: two players asking at once get one answer.
constexpr uint32_t CHANNEL_GAP_S = 10;

// Bots tried for one question: the first one free to talk (out of its own cooldown) answers.
constexpr uint32_t BOTS_TRIED = 5;

inline bool PlayerMayAsk(uint64_t lastS, uint64_t nowS)
{
    return !lastS || nowS >= lastS + PLAYER_CD_S;
}

inline bool ChannelMayAnswer(uint64_t lastS, uint64_t nowS)
{
    return !lastS || nowS >= lastS + CHANNEL_GAP_S;
}

// "What class is the best?" asks for opinions: up to two bots answer it (AiPlayerbot.CoaLive.ChannelQuestions.
// ClassAnswers, 1 or 2), never more, each a different bot. Any other question gets one answer.
constexpr uint32_t MAX_ANSWERS = 2;

inline uint32_t AnswersWanted(Topic topic, uint32_t configured, size_t bots)
{
    // "toujours 2 bots" (3.0.1): any open question gets two answers too.
    uint32_t const wanted = topic == Topic::None ? 0 : std::min<uint32_t>(std::max<uint32_t>(configured, 1), MAX_ANSWERS);
    return static_cast<uint32_t>(std::min<size_t>(wanted, bots));
}

// The second answer comes 4 to 12 seconds after the first is said.
constexpr uint32_t SECOND_MIN_MS = 3000;
constexpr uint32_t SECOND_MAX_MS = 10000;

inline uint64_t SecondDueMs(uint64_t firstDueMs, uint32_t roll)
{
    return firstDueMs + SECOND_MIN_MS + roll % (SECOND_MAX_MS - SECOND_MIN_MS + 1);
}

// What a class answer names first: "bloodmage, paying with..." -> "bloodmage". The second answer names another.
inline std::string Lead(std::string const& text)
{
    std::string lead;
    for (char c : text)
    {
        if (c == ',' || c == '.' || c == '!' || c == '?' || c == ':')
            break;
        lead += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!lead.empty() && lead.back() == ' ')
        lead.pop_back();
    return lead;
}
}  // namespace CoaChanAsk

#endif
