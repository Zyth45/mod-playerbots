/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAHEALREQUESTRULES_H
#define _PLAYERBOT_COAHEALREQUESTRULES_H

// AiPlayerbot.CoaLive.HealRequest.Enable: a real player asks for a heal ("heal me", "need heal", "soigne-moi") in party
// or raid chat, or whispers it to a bot; a healer bot of its group in range casts a heal on it at once. One heal per
// player in PLAYER_CD_MS, none when the player is at full health. Without the game objects, for the unit test.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace CoaHealRequest
{
constexpr uint64_t PLAYER_CD_MS = 15000;  // one heal asked per player in this time
constexpr uint64_t FRESH_MS = 4000;       // a request no healer could answer in this time is dropped
constexpr uint32_t MAX_WORDS = 8;         // a longer line is talk, not a request
constexpr uint32_t FAST_CAST_MS = 2500;   // the heals cast at once are preferred to the slow big ones
constexpr uint32_t MAX_SPELL_CD_MS = 10000;  // a heal with a longer cooldown (Lay on Hands...) is kept for the healer

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

inline bool IsHealWord(std::string const& w)
{
    return w == "heal" || w == "heals" || w == "healz" || w == "healing" || w == "heall" || w == "hea1";
}

inline bool IsSoinWord(std::string const& w) { return w == "soin" || w == "soins" || w == "soigne" || w == "soignez"; }

inline bool IsPlease(std::string const& w)
{
    return w == "me" || w == "plz" || w == "pls" || w == "please" || w == "plox" || w == "pl0x" || w == "stp" ||
           w == "svp" || w == "moi" || w == "now" || w == "asap" || w == "quick" || w == "vite";
}

// A heal asked: "heal me", "heal plz", "need heal", "need a heal", "heals?", "soigne-moi", "soin stp",
// "besoin de soin", "besoin d'un heal", or "heal" alone. Not a line about heals ("nice heal", "can you heal the tank").
inline bool IsHealRequest(std::string const& text)
{
    std::vector<std::string> const words = Words(text);
    if (words.empty() || words.size() > MAX_WORDS)
        return false;
    if (words.size() == 1)
        return IsHealWord(words[0]) || words[0] == "soin" || words[0] == "soins";
    for (size_t i = 0; i < words.size(); ++i)
    {
        std::string const& w = words[i];
        std::string const next = i + 1 < words.size() ? words[i + 1] : std::string();
        // "heal me", "heal plz", "soigne moi", "soin stp"
        if ((IsHealWord(w) || IsSoinWord(w)) && IsPlease(next))
            return true;
        // "need heal", "need a heal", "need some heals", "need healing"
        if (w == "need" || w == "needs")
            for (size_t j = i + 1; j < words.size() && j <= i + 2; ++j)
                if (IsHealWord(words[j]))
                    return true;
        // "besoin de soin", "besoin d'un heal", "besoin de heal"
        if (w == "besoin")
            for (size_t j = i + 1; j < words.size() && j <= i + 3; ++j)
                if (IsHealWord(words[j]) || IsSoinWord(words[j]))
                    return true;
    }
    return false;
}

// A heal is wanted below full health.
inline bool Wanted(uint32_t health, uint32_t maxHealth) { return health && health < maxHealth; }

// The player's last heal asked was at lastMs (0: never).
inline bool CooledDown(uint64_t lastMs, uint64_t nowMs) { return !lastMs || nowMs >= lastMs + PLAYER_CD_MS; }

inline bool Fresh(uint64_t heardMs, uint64_t nowMs) { return nowMs < heardMs + FRESH_MS; }

// A heal the bot knows: its spell id, cast time, level, and whether it heals at once (else over time).
struct HealSpell
{
    uint32_t id = 0;
    uint32_t castMs = 0;
    uint32_t level = 0;
    bool direct = true;
};

// The heal to cast: a direct heal cast fast first, then any direct heal, then a heal over time; the highest level
// (rank) in each. 0: none.
inline uint32_t PickHeal(std::vector<HealSpell> const& spells)
{
    auto rank = [](HealSpell const& s) { return s.direct ? (s.castMs <= FAST_CAST_MS ? 2 : 1) : 0; };
    HealSpell const* best = nullptr;
    for (HealSpell const& s : spells)
        if (!best || rank(s) > rank(*best) || (rank(s) == rank(*best) && s.level > best->level))
            best = &s;
    return best ? best->id : 0;
}
}  // namespace CoaHealRequest

#endif
