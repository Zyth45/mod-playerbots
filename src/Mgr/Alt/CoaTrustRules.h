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

// CoA 3.1.6, the trust of a player in each of his bots (CoaTrust), shown in the Info tab of the SquidBots addon as a
// named rank and a bar. Shown only: no rank opens or locks anything. Pure rules: the gains and the ranks.
namespace CoaTrustRules
{
constexpr uint32_t KILL = 1;         // a monster killed together
constexpr uint32_t DUNGEON_BOSS = 25;
constexpr uint32_t RAID_BOSS = 100;

// The points of a kill: a boss of a raid, a boss elsewhere (a dungeon, a world boss), or any other monster.
inline uint32_t Gain(bool boss, bool raid) { return boss ? (raid ? RAID_BOSS : DUNGEON_BOSS) : KILL; }

struct Rank
{
    char const* key;  // English key, translated by the addon
    uint32_t from;    // the score where it starts
};

constexpr Rank RANKS[] = {
    {"stranger", 0}, {"acquaintance", 100}, {"companion", 500}, {"friend", 1500}, {"trusted", 4000}, {"sworn", 10000},
};
constexpr uint32_t RANK_COUNT = sizeof(RANKS) / sizeof(RANKS[0]);

inline uint32_t RankIndex(uint32_t score)
{
    uint32_t i = 0;
    while (i + 1 < RANK_COUNT && score >= RANKS[i + 1].from)
        ++i;
    return i;
}

// The addon line: "TRUST\t<rank>\t<score>\t<rank start>\t<next rank start>\t<next rank>". At the last rank, the bar
// is full: the end is the score itself and the next rank is "-".
inline std::string Line(uint32_t score)
{
    uint32_t const i = RankIndex(score);
    bool const last = i + 1 >= RANK_COUNT;
    std::ostringstream out;
    out << "TRUST\t" << RANKS[i].key << "\t" << score << "\t" << RANKS[i].from << "\t"
        << (last ? score : RANKS[i + 1].from) << "\t" << (last ? "-" : RANKS[i + 1].key);
    return out.str();
}

// A score never wraps: it stops at the top of uint32.
inline uint32_t Add(uint32_t score, uint32_t gain) { return score > UINT32_MAX - gain ? UINT32_MAX : score + gain; }
}  // namespace CoaTrustRules

#endif
