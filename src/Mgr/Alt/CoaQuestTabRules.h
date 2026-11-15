/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAQUESTTABRULES_H
#define _PLAYERBOT_COAQUESTTABRULES_H

// AiPlayerbot.CoaQuestLog.Enable: the group Quests tab of the SquidBots addon (3.1.3), for each alt of the player
// (SQBA, through the alt's queue):
// - "qlog": its quest log, "QL\tit\t<questId>\t<inprog|done|failed>\t<cur/need;cur/need...>\t<zone>\t<title>" each
//   (MAX_LINES at most), then "QL\t-"; zone: the area's name, or "sort:<id>" for a quest sort (Cooking, a class...);
// - "qlog watch on|off": while on (WATCH_S at most, the addon renews it), a "QL\tit" line when a quest changes and
//   "QL\tgone\t<questId>" when it leaves the log, looked at every 5 s; answered "QL\twatch\t<1|0>";
// - "turnin <questId> [<choice 1..6>]": "QR\t<questId>\t<turned_in|not_complete|not_in_log|no_ender|bags_full|
//   choose_reward>", choose_reward followed by "\t<itemId,itemId...>" (asked again with the choice);
// - "qplan ...": "QP\terr\tlocal" (the quest order stays a reminder of the addon: the bots' travel is not told).
// Off: "QL\terr\toff", "QR\t<questId>\terr\toff". Every line under MAX_LINE characters. Without the game objects.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace CoaQuestTab
{
constexpr uint32_t MAX_LINES = 25;
constexpr size_t MAX_LINE = 250;  // an addon message is 255 characters at most, prefix included
constexpr size_t MAX_TITLE = 80;
constexpr size_t MAX_ZONE = 40;
constexpr uint32_t WATCH_S = 300;
constexpr float ENDER_RANGE = 10.0f;  // the quest ender must be this close for a turn in

inline char const* StateName(bool complete, bool failed) { return failed ? "failed" : complete ? "done" : "inprog"; }

// "cur/need;cur/need", each current count capped at its need.
inline std::string Objectives(std::vector<std::pair<uint32_t, uint32_t>> const& counts)
{
    std::string out;
    for (auto const& [cur, need] : counts)
    {
        if (!need)
            continue;
        if (!out.empty())
            out += ';';
        out += std::to_string(cur < need ? cur : need) + "/" + std::to_string(need);
    }
    return out;
}

// No tab nor line break, at most max bytes, never a UTF-8 character cut in two.
inline std::string Clean(std::string text, size_t max)
{
    for (char& c : text)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    if (text.size() > max)
    {
        while (max > 0 && (static_cast<unsigned char>(text[max]) & 0xC0) == 0x80)
            --max;
        text.resize(max);
    }
    return text;
}

// The QL line; its title is cut shorter when the line would pass MAX_LINE.
inline std::string Line(uint32_t questId, char const* state, std::string const& objectives, std::string const& zone,
                        std::string const& title)
{
    std::string const head = "QL\tit\t" + std::to_string(questId) + "\t" + state + "\t" + Clean(objectives, 120) + "\t" +
                             Clean(zone, MAX_ZONE) + "\t";
    size_t const room = head.size() < MAX_LINE ? MAX_LINE - head.size() : 0;
    return head + Clean(title, room < MAX_TITLE ? room : MAX_TITLE);
}

// "<questId> [<choice 1..6>]" of "turnin". choice 0: none given. false: not understood.
inline bool ParseTurnIn(std::string const& arg, uint32_t& questId, uint32_t& choice)
{
    questId = choice = 0;
    size_t const space = arg.find(' ');
    std::string const id = arg.substr(0, space);
    if (id.empty() || id.size() > 9 || id.find_first_not_of("0123456789") != std::string::npos)
        return false;
    questId = uint32_t(std::strtoul(id.c_str(), nullptr, 10));
    if (space != std::string::npos)
    {
        std::string rest = arg.substr(space + 1);
        while (!rest.empty() && rest.back() == ' ')
            rest.pop_back();
        if (rest.size() != 1 || rest[0] < '1' || rest[0] > '6')
            return false;
        choice = uint32_t(rest[0] - '0');
    }
    return questId != 0;
}

// What a turn in does: the order of the checks.
enum class TurnIn : uint8_t
{
    NotInLog,
    NotComplete,
    NoEnder,
    ChooseReward,
    BagsFull,
    TurnedIn
};

inline char const* TurnInName(TurnIn t)
{
    switch (t)
    {
        case TurnIn::NotInLog:
            return "not_in_log";
        case TurnIn::NotComplete:
            return "not_complete";
        case TurnIn::NoEnder:
            return "no_ender";
        case TurnIn::ChooseReward:
            return "choose_reward";
        case TurnIn::BagsFull:
            return "bags_full";
        default:
            return "turned_in";
    }
}

inline TurnIn Decide(bool inLog, bool complete, bool enderNear, uint32_t rewardChoices, uint32_t choice, bool bagRoom)
{
    if (!inLog)
        return TurnIn::NotInLog;
    if (!complete)
        return TurnIn::NotComplete;
    if (!enderNear)
        return TurnIn::NoEnder;
    if (rewardChoices > 1 && (!choice || choice > rewardChoices))
        return TurnIn::ChooseReward;
    if (!bagRoom)
        return TurnIn::BagsFull;
    return TurnIn::TurnedIn;
}

// "qlog watch on|off"; false: not understood.
inline bool ParseWatch(std::string const& arg, bool& on)
{
    if (arg == "watch on")
        on = true;
    else if (arg == "watch off")
        on = false;
    else
        return false;
    return true;
}

inline bool Watching(int64_t untilS, int64_t nowS) { return untilS > nowS; }
}  // namespace CoaQuestTab

#endif
