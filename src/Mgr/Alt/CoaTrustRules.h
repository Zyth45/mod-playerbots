/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COATRUSTRULES_H
#define PLAYERBOTS_COATRUSTRULES_H

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// CoA 3.1.6, the trust of a player in each of his bots (CoaTrust), shown in the Info tab of the SquidBots addon as a
// named rank and a bar. Shown only: no rank opens or locks anything. Pure rules: the gains and the ranks, both set in
// playerbots.conf (AiPlayerbot.CoaTrust.*).
namespace CoaTrustRules
{
constexpr uint32_t RANK_COUNT = 6;
// English keys, translated by the addon.
constexpr char const* RANK_KEYS[RANK_COUNT] = {"stranger", "acquaintance", "companion", "friend", "trusted", "sworn"};

struct Settings
{
    uint32_t kill = 1;        // a monster killed together
    uint32_t boss = 25;       // a boss outside a raid (a dungeon, a world boss)
    uint32_t raidBoss = 100;  // a boss of a raid
    uint32_t from[RANK_COUNT] = {0, 100, 500, 1500, 4000, 10000};  // where each rank starts
};

// The points of a kill: a boss of a raid, a boss elsewhere, or any other monster.
inline uint32_t Gain(Settings const& s, bool boss, bool raid) { return boss ? (raid ? s.raidBoss : s.boss) : s.kill; }

// The rank starts from a list "0,100,500,1500,4000,10000": six numbers, the first 0, each above the one before.
// False (and from untouched) otherwise.
inline bool ParseRanks(std::string const& text, uint32_t (&from)[RANK_COUNT])
{
    std::vector<uint32_t> values;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, ',');)
    {
        size_t const start = part.find_first_not_of(' ');
        if (start == std::string::npos)
            return false;
        uint64_t value = 0;
        for (size_t i = start; i < part.size() && part[i] != ' '; ++i)
        {
            if (part[i] < '0' || part[i] > '9')
                return false;
            value = value * 10 + uint64_t(part[i] - '0');
            if (value > UINT32_MAX)
                return false;
        }
        values.push_back(uint32_t(value));
    }
    if (values.size() != RANK_COUNT || values[0] != 0)
        return false;
    for (uint32_t i = 1; i < RANK_COUNT; ++i)
        if (values[i] <= values[i - 1])
            return false;
    for (uint32_t i = 0; i < RANK_COUNT; ++i)
        from[i] = values[i];
    return true;
}

inline uint32_t RankIndex(Settings const& s, uint32_t score)
{
    uint32_t i = 0;
    while (i + 1 < RANK_COUNT && score >= s.from[i + 1])
        ++i;
    return i;
}

// The addon line: "TRUST\t<rank>\t<score>\t<rank start>\t<next rank start>\t<next rank>". At the last rank, the bar
// is full: the end is the score itself and the next rank is "-".
inline std::string Line(Settings const& s, uint32_t score)
{
    uint32_t const i = RankIndex(s, score);
    bool const last = i + 1 >= RANK_COUNT;
    std::ostringstream out;
    out << "TRUST\t" << RANK_KEYS[i] << "\t" << score << "\t" << s.from[i] << "\t" << (last ? score : s.from[i + 1])
        << "\t" << (last ? "-" : RANK_KEYS[i + 1]);
    return out.str();
}

// A score never wraps: it stops at the top of uint32.
inline uint32_t Add(uint32_t score, uint32_t gain) { return score > UINT32_MAX - gain ? UINT32_MAX : score + gain; }
}  // namespace CoaTrustRules

#endif
