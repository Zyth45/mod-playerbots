/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAPLAYERREPLYRULES_H
#define _PLAYERBOT_COAPLAYERREPLYRULES_H

// AiPlayerbot.CoaLive.PlayerReplies.Enable: the bots answer what a real player does next to them or in their group
// (study "monde vivant", 3.0.1): an emote at them, a death near them (a fall), a fine drop of the player, a wipe, an
// insult, a declined invitation, a player away, a player joining, a quest shared, a fine mount. Two bots answer what
// a player does ("toujours 2 bots"), one only for an insult, a declined invitation, a player away and a mount. Without
// the game objects, for the unit test.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace CoaPlayerReply
{
// The text emotes a bot answers, and the one it gives back (the client's TEXT_EMOTE_* ids): a wave for a wave or a
// hello, a bow for a bow or a salute, a thank-you's "you're welcome" bow, a cheer for a cheer, a hug for a hug. 0: none.
inline uint32_t EmoteBack(uint32_t textEmote)
{
    constexpr uint32_t BOW = 17, CHEER = 21, GREET = 48, HELLO = 55, HUG = 56, SALUTE = 78, THANK = 97, WAVE = 101;
    switch (textEmote)
    {
        case WAVE:
        case HELLO:
        case GREET:
            return WAVE;
        case BOW:
        case SALUTE:
        case THANK:
            return BOW;
        case CHEER:
            return CHEER;
        case HUG:
            return HUG;
        default:
            return 0;
    }
}

// How many bots answer: two, fewer when fewer are there; one for the replies of a single bot.
inline uint32_t Speakers(size_t bots, bool single) { return static_cast<uint32_t>(std::min<size_t>(single ? 1 : 2, bots)); }

// The cooldowns, in seconds, per player (or per group for a wipe).
constexpr uint32_t EMOTE_PLAYER_CD_S = 30;
constexpr uint32_t EMOTE_BOT_CD_S = 120;
constexpr uint32_t DIED_CD_S = 5 * 60;
constexpr uint32_t FALL_CD_S = 10 * 60;
constexpr uint32_t WIPE_CD_S = 10 * 60;
constexpr uint32_t INSULT_CD_S = 30 * 60;
constexpr uint32_t QUEST_CD_S = 5 * 60;
constexpr uint32_t MOUNT_CD_S = 30 * 60;
constexpr uint32_t JOIN_CD_S = 10 * 60;
constexpr uint32_t AFK_AFTER_S = 3 * 60;  // a player away this long in a group with bots
inline bool CooledDown(uint64_t lastS, uint64_t nowS, uint32_t cdS) { return !lastS || nowS >= lastS + cdS; }

// A fall that kills: the damage the fall did, the health the player had.
inline bool DeadlyFall(uint32_t damage, uint32_t health) { return health && damage >= health; }

// A fine drop of the player: epic, or rare below level 60.
inline bool FineDrop(uint32_t quality, uint32_t playerLevel) { return quality >= 4 || (quality == 3 && playerLevel < 60); }

// A fine mount: an epic ground speed or a flying mount (run speed rate at least 2, +100%).
inline bool FineMount(bool mounted, float runSpeedRate) { return mounted && runSpeedRate >= 1.99f; }

// An insult at a bot: a short line (8 words at most) with one of these words or phrases. Never a reproach of play
// ("bad pull"), never "bot" itself: the players' jokes about bots are theirs to make.
inline std::vector<std::string> Words(std::string const& text)
{
    std::vector<std::string> words;
    std::string word;
    for (char ch : text)
    {
        unsigned char const c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c >= 0x80)
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

inline bool IsInsult(std::string const& text)
{
    std::vector<std::string> const words = Words(text);
    if (words.empty() || words.size() > 8)
        return false;
    static std::set<std::string> const insults = {"idiot", "stupid", "moron", "dumb", "dumbass", "loser", "noob",
                                                  "nub", "stfu", "trash", "garbage", "useless", "imbecile", "con",
                                                  "connard", "conne", "débile", "debile", "abruti", "abrutie",
                                                  "crétin", "cretin", "boulet", "nul", "nulle", "tg", "dégage",
                                                  "degage", "pathétique", "pathetic"};
    for (size_t i = 0; i < words.size(); ++i)
    {
        if (insults.count(words[i]))
            return true;
        if (i + 1 < words.size() &&
            ((words[i] == "shut" && words[i + 1] == "up") || (words[i] == "ta" && words[i + 1] == "gueule") ||
             (words[i] == "fuck" && words[i + 1] == "you") || (words[i] == "ferme" && words[i + 1] == "la")))
            return true;
    }
    return false;
}
}  // namespace CoaPlayerReply

#endif
