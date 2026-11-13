/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COALOOTASKRULES_H
#define _PLAYERBOT_COALOOTASKRULES_H

// AiPlayerbot.CoaLive.LootNeedAsk: in a dungeon, a bot grouped with a real player asks in party chat whether it may
// roll Need on an item that is a real upgrade for it ("can i need on that? its bis for me"). The rules, without the
// game objects, for the unit test.
//
// The Need goes out on an explicit yes only: a short message (4 words at most) of a real player of the group, in the
// window. A no, an ambiguous message, the end of the window: Greed, as without the ask. With several real players a
// single no wins, and the Need waits for every one of them to say yes or for the end of the window.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace CoaLootAsk
{
enum class Answer
{
    None,  // not an answer (too long, both, neither)
    Yes,
    No
};

constexpr size_t MAX_WORDS = 4;

inline std::vector<std::string> Words(std::string const& text)
{
    std::vector<std::string> words;
    std::string word;
    for (char ch : text)
    {
        unsigned char const c = static_cast<unsigned char>(ch);
        // Letters (UTF-8 bytes kept as they are: "ouais"), digits and the apostrophe of "don't" make words.
        if (std::isalnum(c) || c >= 0x80 || c == '\'')
            word += static_cast<char>(std::tolower(c));
        else if (!word.empty())
        {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty())
        words.push_back(word);
    return words;
}

inline Answer Classify(std::string const& text)
{
    std::vector<std::string> const words = Words(text);
    if (words.empty() || words.size() > MAX_WORDS)
        return Answer::None;
    static std::set<std::string> const yes = {"yes", "y", "yep", "yup", "yeah", "ya", "yea", "sure", "ok", "okay", "k",
                                              "kk", "go", "np", "oui", "ouais", "ouai", "vasy"};
    // Not "need", "take" nor "prends": "i need it", "je le prends" are the player's own claim.
    static std::set<std::string> const no = {"no", "n", "nope", "nah", "non", "nan", "dont", "don't", "pas", "nop",
                                             "noo", "nooo"};
    bool sawYes = false;
    bool sawNo = false;
    for (size_t i = 0; i < words.size(); ++i)
    {
        std::string const& w = words[i];
        if (no.count(w))
            sawNo = true;
        else if (yes.count(w))
            sawYes = true;
        else if (w == "vas" && i + 1 < words.size() && words[i + 1] == "y")  // "vas-y", "vas y"
            sawYes = true;
    }
    if (sawNo)
        return sawYes ? Answer::None : Answer::No;
    return sawYes ? Answer::Yes : Answer::None;
}

// The window to answer in, from the moment the bot saw the roll open (the core's roll lasts 60 s).
constexpr uint32_t WINDOW_MS = 40000;
// A bot asks only at the opening of the roll, so the players have the whole window.
constexpr uint32_t ASK_WITHIN_MS = 5000;

enum class Outcome
{
    NotAsked,  // no ask on this roll for this bot: the usual vote
    Wait,      // asked, no answer yet: the vote waits
    Need,      // every real player said yes (or a yes and no no at the end of the window)
    Greed      // a no, or nothing by the end of the window
};

struct Ask
{
    uint64_t bot = 0;
    uint64_t deadlineMs = 0;
    std::set<uint64_t> players;  // the real players of the group when the bot asked
    std::set<uint64_t> yes;
    bool no = false;
    bool decided = false;
    uint64_t instance = 0;  // for the bench log
    uint32_t itemId = 0;
};

// The answer the ask got, for the bench log: "no", "yes" or "none".
inline char const* AnswerName(Ask const& ask)
{
    return ask.no ? "no" : !ask.yes.empty() ? "yes" : "none";
}

inline Outcome Decide(Ask const& ask, uint64_t nowMs)
{
    if (ask.no)
        return Outcome::Greed;
    if (!ask.players.empty() && ask.yes.size() >= ask.players.size())
        return Outcome::Need;
    if (nowMs >= ask.deadlineMs)
        return ask.yes.empty() ? Outcome::Greed : Outcome::Need;
    return Outcome::Wait;
}

// A real player's message heard while asks are open: counted for each open ask the player is part of.
inline void Hear(Ask& ask, uint64_t player, Answer answer, uint64_t nowMs)
{
    if (ask.decided || nowMs >= ask.deadlineMs || !ask.players.count(player))
        return;
    if (answer == Answer::No)
        ask.no = true;
    else if (answer == Answer::Yes)
        ask.yes.insert(player);
}

// The asks of all the groups: at most `perInstance` per dungeon instance for all the bots, one bot per roll.
class Book
{
public:
    // Opens an ask of `bot` on `roll` in `instance`: false when the instance has had its share, or another bot
    // already asks on that roll.
    bool Open(uint64_t instance, uint64_t roll, Ask const& ask, uint32_t perInstance)
    {
        if (asks.count(roll) || count[instance] >= perInstance)
            return false;
        ++count[instance];
        asks[roll] = ask;
        return true;
    }

    bool CanOpen(uint64_t instance, uint64_t roll, uint32_t perInstance) const
    {
        auto const itr = count.find(instance);
        return !asks.count(roll) && (itr == count.end() ? 0u : itr->second) < perInstance;
    }

    Ask* Find(uint64_t roll)
    {
        auto const itr = asks.find(roll);
        return itr == asks.end() ? nullptr : &itr->second;
    }

    std::map<uint64_t, Ask>& All() { return asks; }

    // Rolls long over (twice the window past their deadline) are forgotten; the share of an instance stays.
    void Forget(uint64_t nowMs)
    {
        for (auto itr = asks.begin(); itr != asks.end();)
            itr = nowMs > itr->second.deadlineMs + 2 * WINDOW_MS ? asks.erase(itr) : std::next(itr);
        if (count.size() > 20000)
            count.clear();
    }

private:
    std::map<uint64_t, Ask> asks;          // roll -> its ask
    std::map<uint64_t, uint32_t> count;    // instance -> asks made there
};
}  // namespace CoaLootAsk

#endif
