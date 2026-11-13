/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAGROUPEMOTERULES_H
#define PLAYERBOTS_COAGROUPEMOTERULES_H

#include <cstdint>
#include <deque>
#include <unordered_map>

// AiPlayerbot.CoaLive.GroupEmotes.PerBot / PerGroup: the text emotes ("Kegarink cheers.") of the bots of a group with a
// real player, at most so many per bot and per group in a window. Without the game objects, for the unit test.
namespace CoaGroupEmote
{
constexpr uint64_t WINDOW_S = 600;

class Cap
{
public:
    // True, and counted, when the bot may emote now; 0 for a cap: no cap.
    bool Take(uint64_t bot, uint64_t group, uint64_t nowS, uint32_t perBot, uint32_t perGroup)
    {
        std::deque<uint64_t>& b = bots_[bot];
        std::deque<uint64_t>& g = groups_[group];
        Expire(b, nowS);
        Expire(g, nowS);
        if ((perBot && b.size() >= perBot) || (perGroup && g.size() >= perGroup))
            return false;
        b.push_back(nowS);
        g.push_back(nowS);
        if (++taken_ % 1024 == 0)
            Prune(nowS);
        return true;
    }

    size_t Size() const { return bots_.size() + groups_.size(); }

private:
    static void Expire(std::deque<uint64_t>& q, uint64_t nowS)
    {
        while (!q.empty() && q.front() + WINDOW_S <= nowS)
            q.pop_front();
    }

    void Prune(uint64_t nowS)
    {
        for (auto* map : {&bots_, &groups_})
            for (auto it = map->begin(); it != map->end();)
            {
                Expire(it->second, nowS);
                it = it->second.empty() ? map->erase(it) : std::next(it);
            }
    }

    std::unordered_map<uint64_t, std::deque<uint64_t>> bots_;
    std::unordered_map<uint64_t, std::deque<uint64_t>> groups_;
    uint64_t taken_ = 0;
};
}  // namespace CoaGroupEmote

#endif
