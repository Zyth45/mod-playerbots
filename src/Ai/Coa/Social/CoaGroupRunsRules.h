/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAGROUPRUNSRULES_H
#define PLAYERBOTS_COAGROUPRUNSRULES_H

#include "CoaWorldFileRules.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

// world.json "groups" (D:\CoA-Dashboard\docs\LIAISON-3.0.md §13): the groups in a dungeon or a raid, players, alts
// and bots alike. A run starts when a group is first seen in an instance and ends after RUN_GONE_S outside it; a
// boss kill and a wipe are counted by the hooks. Pure rules, fed by CoaWorldFile.
namespace CoaWorldFile
{
constexpr int64_t RUN_GONE_S = 300;
constexpr size_t RUNS_LAST = 8;

struct Run
{
    uint32_t instance = 0;
    uint32_t map = 0;
    std::string name;  // the map's name
    bool raid = false;
    uint32_t difficulty = 0;
    Mix mix;
    std::string leader;
    int64_t startedAt = 0;
    int64_t lastSeen = 0;
    int64_t endedAt = 0;
    std::set<uint32_t> bosses;  // the boss entries killed
    int32_t bossesTotal = -1;   // -1: not known
    uint32_t wipes = 0;
    bool cleared = false;
    bool lastWasWipe = false;

    char const* Result() const { return cleared ? "clear" : lastWasWipe ? "wipe" : "left"; }
};

class RunBook
{
public:
    // The 10 s pass: a group seen in this instance now.
    void Seen(Run const& now, int64_t at)
    {
        Run& r = running_[now.instance];
        if (!r.startedAt)
        {
            r = now;
            r.startedAt = at;
        }
        r.mix = now.mix;
        r.leader = now.leader;
        if (now.bossesTotal >= 0)
            r.bossesTotal = now.bossesTotal;
        r.lastSeen = at;
    }

    // A boss down; cleared: the instance holds no boss alive any more (its last).
    void Kill(uint32_t instance, uint32_t boss, bool cleared, int64_t at)
    {
        auto it = running_.find(instance);
        if (it == running_.end())
            return;  // a group not seen yet (killed within 10 s of entering): counted from the next pass
        if (it->second.bosses.insert(boss).second)
            ++today_.bossKills;  // a boss counted once a run
        it->second.lastWasWipe = false;
        it->second.cleared = it->second.cleared || cleared;
        (void)at;
    }

    void Wipe(uint32_t instance)
    {
        auto it = running_.find(instance);
        if (it == running_.end())
            return;
        ++it->second.wipes;
        it->second.lastWasWipe = true;
        ++today_.wipes;
    }

    // The runs whose group has been gone RUN_GONE_S end; "today" starts again at midnight.
    void Close(int64_t at, int64_t todaySince)
    {
        if (todaySince != today_.since)
            today_ = Today{todaySince};
        for (auto it = running_.begin(); it != running_.end();)
        {
            if (at < it->second.lastSeen + RUN_GONE_S)
            {
                ++it;
                continue;
            }
            Run r = std::move(it->second);
            it = running_.erase(it);
            r.endedAt = r.lastSeen;
            if (r.startedAt >= today_.since)
            {
                ++today_.runs;
                today_.cleared += r.cleared;
            }
            Stats& s = byMap_[r.map];
            s.name = r.name;
            ++s.runs;
            s.cleared += r.cleared;
            s.wipes += r.wipes;
            if (r.cleared)
            {
                s.clearS.push_back(r.endedAt - r.startedAt);
                if (s.clearS.size() > 200)
                    s.clearS.erase(s.clearS.begin());
            }
            last_.push_front(std::move(r));
            if (last_.size() > RUNS_LAST)
                last_.pop_back();
        }
    }

    std::string Json() const
    {
        std::string out = "{\"running\":[";
        bool first = true;
        for (auto const& [instance, r] : running_)
        {
            out += std::string(first ? "" : ",") + "{\"instance\":" + std::to_string(r.instance) + ",\"map\":" +
                   std::to_string(r.map) + ",\"dungeon\":null,\"dungeonName\":" + Quote(r.name) + ",\"kind\":" +
                   (r.raid ? "\"raid\"" : "\"dungeon\"") + ",\"difficulty\":" + std::to_string(r.difficulty) + "," +
                   MixJson(r.mix).substr(1, MixJson(r.mix).size() - 2) + ",\"leader\":" + Quote(r.leader) +
                   ",\"startedAt\":" + std::to_string(r.startedAt) + ",\"bossesKilled\":" +
                   std::to_string(r.bosses.size()) + ",\"bossesTotal\":" + Num(r.bossesTotal) + ",\"wipes\":" +
                   std::to_string(r.wipes) + ",\"dc\":null}";
            first = false;
        }
        out += "],\"today\":{\"since\":" + std::to_string(today_.since) + ",\"runs\":" + std::to_string(today_.runs) +
               ",\"cleared\":" + std::to_string(today_.cleared) + ",\"wipes\":" + std::to_string(today_.wipes) +
               ",\"bossKills\":" + std::to_string(today_.bossKills) + "},\"byDungeon\":[";
        first = true;
        for (auto const& [map, s] : byMap_)
        {
            std::vector<int64_t> sorted = s.clearS;
            std::sort(sorted.begin(), sorted.end());
            int64_t const median = sorted.empty() ? -1 : sorted[sorted.size() / 2];
            int64_t const best = sorted.empty() ? -1 : sorted.front();
            out += std::string(first ? "" : ",") + "{\"dungeon\":null,\"map\":" + std::to_string(map) +
                   ",\"dungeonName\":" + Quote(s.name) + ",\"runs\":" + std::to_string(s.runs) + ",\"cleared\":" +
                   std::to_string(s.cleared) + ",\"wipes\":" + std::to_string(s.wipes) + ",\"medianS\":" +
                   Num(median) + ",\"bestS\":" + Num(best) + "}";
            first = false;
        }
        out += "],\"last\":[";
        for (size_t i = 0; i < last_.size(); ++i)
        {
            Run const& r = last_[i];
            out += std::string(i ? "," : "") + "{\"instance\":" + std::to_string(r.instance) + ",\"dungeon\":null," +
                   "\"map\":" + std::to_string(r.map) + ",\"dungeonName\":" + Quote(r.name) + ",\"difficulty\":" +
                   std::to_string(r.difficulty) + ",\"startedAt\":" + std::to_string(r.startedAt) + ",\"endedAt\":" +
                   std::to_string(r.endedAt) + ",\"result\":\"" + r.Result() + "\",\"bossesKilled\":" +
                   std::to_string(r.bosses.size()) + ",\"bossesTotal\":" + Num(r.bossesTotal) + ",\"wipes\":" +
                   std::to_string(r.wipes) + "," + MixJson(r.mix).substr(1, MixJson(r.mix).size() - 2) +
                   ",\"dc\":null}";
        }
        return out + "]}";
    }

    size_t Running() const { return running_.size(); }

private:
    struct Today
    {
        int64_t since = 0;
        uint32_t runs = 0;
        uint32_t cleared = 0;
        uint32_t wipes = 0;
        uint32_t bossKills = 0;
    };
    struct Stats
    {
        std::string name;
        uint32_t runs = 0;
        uint32_t cleared = 0;
        uint32_t wipes = 0;
        std::vector<int64_t> clearS;
    };
    std::map<uint32_t, Run> running_;  // instance -> run
    std::map<uint32_t, Stats> byMap_;
    std::deque<Run> last_;
    Today today_;
};

// "today" starts at the server's midnight, or at the server's start when that is later.
inline int64_t TodaySince(int64_t localMidnight, int64_t serverStart) { return std::max(localMidnight, serverStart); }
}  // namespace CoaWorldFile

#endif
