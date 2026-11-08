/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COABOTIDENTIFYRULES_H
#define PLAYERBOTS_COABOTIDENTIFYRULES_H

#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <utility>

// Bot detection without the "Bot" surname (protocol BOT-DETECTION-3.0 of the SquidBots Lite addon): the addon asks
// its group "WHO?" on the addon prefix SQBA, every playerbot of the group whispers "IAM <kind> <master>" back, as an
// addon message the chat never shows. Pure rules, no game object, so that they can be tested alone.

// The text of a group addon message that asks who is a bot: prefix SQBA, then "WHO?".
inline bool CoaIdentifyIsWho(std::string const& msg)
{
    static std::string const who = "SQBA\tWHO?";
    if (msg.compare(0, who.size(), who) != 0)
        return false;
    for (size_t i = who.size(); i < msg.size(); ++i)
        if (msg[i] != ' ' && msg[i] != '\t' && msg[i] != '\r' && msg[i] != '\n')
            return false;
    return true;
}

// The answer, without the prefix: "IAM\trandom\t-" for a random bot, "IAM\talt\t<master>" for a player's bot ("-"
// when it has none). A tab or a line break in the master's name could not happen; it would break the fields.
inline std::string CoaIdentifyAnswer(bool randomBot, std::string const& master)
{
    if (randomBot)
        return "IAM\trandom\t-";
    std::string name = master.empty() ? "-" : master;
    for (char& c : name)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    return "IAM\talt\t" + name;
}

// AiPlayerbot.CoaBotIdentify, or a surname that no longer says "Bot" (CoaBotSurname other than 1): without the
// answer, the addon would lose the bots of its group.
inline bool CoaIdentifyEnabled(bool option, uint32_t surnameMode) { return option || surnameMode != 1; }

// One answer per bot and asker every windowMs at most (the addon asks at most every 10 s).
class CoaIdentifyThrottle
{
public:
    explicit CoaIdentifyThrottle(uint32_t windowMs = 5000, size_t pruneAbove = 4096)
        : windowMs_(windowMs), pruneAbove_(pruneAbove)
    {
    }

    bool Allow(uint64_t bot, uint64_t asker, uint64_t nowMs)
    {
        auto const key = std::make_pair(bot, asker);
        auto it = last_.find(key);
        if (it != last_.end() && nowMs < it->second + windowMs_)
            return false;
        last_[key] = nowMs;
        if (last_.size() > pruneAbove_)
            Prune(nowMs);
        return true;
    }

    size_t Size() const { return last_.size(); }

private:
    void Prune(uint64_t nowMs)
    {
        for (auto it = last_.begin(); it != last_.end();)
            it = nowMs >= it->second + windowMs_ ? last_.erase(it) : std::next(it);
    }

    uint32_t windowMs_;
    size_t pruneAbove_;
    std::map<std::pair<uint64_t, uint64_t>, uint64_t> last_;
};

#endif
