/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASOULADDONRULES_H
#define PLAYERBOTS_COASOULADDONRULES_H

#include "CoaSoulRules.h"

#include <cstdint>
#include <string>
#include <unordered_map>

// The soul of a bot for SquidBots Lite (D:\CoA-Addon\docs\WORLD-PROTOCOLE-3.0.md §0, §3), behind
// AiPlayerbot.CoaSoul.Enable and AiPlayerbot.CoaSoul.Addon. Pure rules: the request, the answer, the rate limit.
namespace CoaSoulAddon
{
constexpr char const* PREFIX = "SQBW\t";

// "SQBW\tSOUL?" (whispered to the bot) or "SQBW\tSOUL?\t<name>" (whispered to oneself). True for a soul request;
// name: the name asked, empty when none.
inline bool ParseRequest(std::string const& msg, std::string& name)
{
    std::string const head = std::string(PREFIX) + "SOUL?";
    if (msg.compare(0, head.size(), head) != 0)
        return false;
    std::string rest = msg.substr(head.size());
    if (!rest.empty() && rest[0] != '\t')
        return false;  // "SOUL?x": another word
    name = rest.empty() ? "" : rest.substr(1);
    size_t const tab = name.find('\t');
    if (tab != std::string::npos)
        name.resize(tab);
    return true;
}

// The lines of the answer, without the prefix: the soul, then "-".
inline std::string Line(std::string const& name, CoaSoul::Soul const& s)
{
    std::string line = "SOUL\t" + name + "\t" + CoaSoul::ArchetypeName(s.archetype) + "\t" + std::to_string(s.pace) +
                       "\t" + std::to_string(s.sociability) + "\t" + std::to_string(s.curiosity) + "\t" +
                       std::to_string(s.greed) + "\t" + std::to_string(s.aggression);
    if (line.size() > 250)
        line.resize(250);  // 255 bytes with the prefix: cut, never split
    return line;
}

inline std::string NotYours() { return "ERR\tSOUL?\tnotyours"; }

// At most one request a second and 20 a minute per player, and one answer per bot and player every 10 s; more is
// dropped without an answer.
class Throttle
{
public:
    bool Allow(uint64_t player, uint64_t bot, uint64_t nowMs)
    {
        Count& c = players[player];
        if (c.lastMs && nowMs < c.lastMs + 1000)
            return false;
        if (!c.minuteStartMs || nowMs >= c.minuteStartMs + 60000)
        {
            c.minuteStartMs = nowMs;
            c.inMinute = 0;
        }
        if (c.inMinute >= 20)
            return false;
        uint64_t const pair = player * 1000003ull ^ bot;
        auto it = pairs.find(pair);
        if (it != pairs.end() && nowMs < it->second + 10000)
            return false;
        c.lastMs = nowMs;
        ++c.inMinute;
        pairs[pair] = nowMs;
        if (pairs.size() > 20000)
            pairs.clear();
        if (players.size() > 20000)
            players.clear();
        return true;
    }

private:
    struct Count
    {
        uint64_t lastMs = 0;
        uint64_t minuteStartMs = 0;
        uint32_t inMinute = 0;
    };
    std::unordered_map<uint64_t, Count> players;
    std::unordered_map<uint64_t, uint64_t> pairs;
};
}  // namespace CoaSoulAddon

#endif
