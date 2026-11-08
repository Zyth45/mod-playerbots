/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASOULRULES_H
#define PLAYERBOTS_COASOULRULES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

// The souls of the bots (contract ACCROCHES-LLM-3.0 §1, CoA IA MIND): an archetype and five traits, drawn once from
// the bot's guid (the same at every start), that tilt what the bot likes to do. 3.0: rules only; 3.1 adds a persona
// written by the LLM. Pure rules.
namespace CoaSoul
{
enum Archetype : uint8_t
{
    LAZY = 0,
    QUESTER,
    WANDERER,
    TRADER,
    PVP,
    ARCHETYPE_COUNT,
};

inline char const* ArchetypeName(uint8_t a)
{
    static char const* const names[] = {"lazy", "quester", "wanderer", "trader", "pvp"};
    return a < ARCHETYPE_COUNT ? names[a] : "quester";
}

struct Soul
{
    uint8_t archetype = QUESTER;
    uint8_t pace = 50;         // how fast it levels
    uint8_t sociability = 50;  // groups, greets, talks
    uint8_t curiosity = 50;    // explores, changes zone
    uint8_t greed = 50;        // auction house, gathering, trade
    uint8_t aggression = 50;   // world PvP, duels
    uint32_t seed = 0;
};

using Weights = std::array<uint32_t, ARCHETYPE_COUNT>;

inline Weights DefaultWeights() { return {20, 40, 20, 10, 10}; }

// "20,40,20,10,10": five numbers, at least one above 0; anything else gives the default.
inline Weights ParseWeights(std::string const& text)
{
    Weights w{};
    size_t n = 0, start = 0;
    uint32_t total = 0;
    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        std::string v = text.substr(start, end - start);
        size_t const a = v.find_first_not_of(" \t\""), b = v.find_last_not_of(" \t\"");
        v = a == std::string::npos ? "" : v.substr(a, b - a + 1);
        if (!v.empty())
        {
            if (n >= ARCHETYPE_COUNT || v.size() > 4 || v.find_first_not_of("0123456789") != std::string::npos)
                return DefaultWeights();
            w[n] = static_cast<uint32_t>(std::stoul(v));
            total += w[n++];
        }
        start = end + 1;
    }
    return n == ARCHETYPE_COUNT && total ? w : DefaultWeights();
}

inline uint64_t Mix(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// The profile of each archetype: pace, sociability, curiosity, greed, aggression.
inline std::array<uint8_t, 5> Profile(uint8_t archetype)
{
    static std::array<uint8_t, 5> const profiles[ARCHETYPE_COUNT] = {
        {25, 50, 35, 35, 30},  // lazy
        {75, 40, 45, 40, 40},  // quester
        {50, 70, 80, 40, 35},  // wanderer
        {45, 65, 40, 85, 25},  // trader
        {60, 45, 50, 40, 85},  // pvp
    };
    return profiles[archetype < ARCHETYPE_COUNT ? archetype : QUESTER];
}

// The soul of a bot: the archetype by the weights, the traits within 15 of its profile, all from the guid.
inline Soul Draw(uint64_t guid, Weights const& weights)
{
    Soul s;
    uint64_t z = Mix(guid);
    s.seed = static_cast<uint32_t>(z);
    uint32_t total = 0;
    for (uint32_t w : weights)
        total += w;
    uint32_t roll = total ? static_cast<uint32_t>(z % total) : 0;
    s.archetype = QUESTER;
    for (uint8_t a = 0; a < ARCHETYPE_COUNT; ++a)
    {
        if (roll < weights[a])
        {
            s.archetype = a;
            break;
        }
        roll -= weights[a];
    }
    std::array<uint8_t, 5> const p = Profile(s.archetype);
    uint8_t* traits[5] = {&s.pace, &s.sociability, &s.curiosity, &s.greed, &s.aggression};
    for (int i = 0; i < 5; ++i)
    {
        z = Mix(z + i);
        int const shift = static_cast<int>(z % 31) - 15;  // -15..15
        *traits[i] = static_cast<uint8_t>(std::clamp(int(p[i]) + shift, 0, 100));
    }
    return s;
}

// The weight of a "new rpg" activity for this soul, in percent of the configured one (contract §3): the lazy rest
// and visit the city twice as often, the traders go to the city and its NPCs, the PvP go to world PvP three times as
// often, the wanderers roam and fly.
inline uint32_t RpgPercent(uint8_t archetype, std::string const& status)
{
    switch (archetype)
    {
        case LAZY: return status == "rest" || status == "city" ? 200 : 100;
        case TRADER: return status == "city" || status == "wander_npc" ? 200 : 100;
        case PVP: return status == "pvp" ? 300 : 100;
        case WANDERER: return status == "wander_random" || status == "flight" ? 200 : 100;
        default: return 100;
    }
}
}  // namespace CoaSoul

#endif
