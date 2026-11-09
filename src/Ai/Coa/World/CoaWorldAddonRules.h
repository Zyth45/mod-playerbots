/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of the SquidBots Lite world protocol, prefix SQBW (D:\CoA-Addon\docs\WORLD-PROTOCOLE-3.0.md,
// §0, §1, §4). No game type: tested in tests/coa_world_addon_rules.cpp.

#ifndef PLAYERBOTS_COAWORLDADDONRULES_H
#define PLAYERBOTS_COAWORLDADDONRULES_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace CoaWorldAddonRules
{
constexpr char const* PREFIX = "SQBW";
constexpr size_t MAX_LINE = 255 - 5;      // an addon message holds 255 bytes with "SQBW\t"
constexpr uint32_t MIN_GAP_MS = 1000;     // one request per second per player
constexpr uint32_t CACHE_MS = 10000;      // list answers kept 10 s per player
constexpr int64_t PUSH_FOR_S = 30 * 60;   // WE pushed to players whose last WEV? is under 30 min

inline std::vector<std::string> Split(std::string const& text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (true)
    {
        size_t const tab = text.find('\t', start);
        out.push_back(text.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos)
            return out;
        start = tab + 1;
    }
}

// The request of an addon message "SQBW\t<word>\t<args>": its fields after the prefix, empty if not SQBW.
inline std::vector<std::string> Request(std::string const& msg)
{
    std::vector<std::string> fields = Split(msg);
    if (fields.size() < 2 || fields[0] != PREFIX)
        return {};
    fields.erase(fields.begin());
    return fields;
}

// The words this session answers; the others (artisans, soul) are left to their owners.
inline bool Mine(std::string const& word)
{
    return word == "GORD" || word == "GLIST" || word == "WEV?" || word == "GOFF" || word == "GACC" || word == "GREF";
}

inline bool RateOk(uint64_t lastMs, uint64_t nowMs) { return lastMs == 0 || nowMs - lastMs >= MIN_GAP_MS; }

inline bool CacheFresh(uint64_t builtMs, uint64_t nowMs) { return builtMs != 0 && nowMs - builtMs < CACHE_MS; }

inline bool PushWanted(int64_t lastWev, int64_t now) { return lastWev > 0 && now >= lastWev && now - lastWev < PUSH_FOR_S; }

// A positive number of at most 9 digits, else 0.
inline uint32_t Number(std::string const& text)
{
    if (text.empty() || text.size() > 9 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return 0;
    return uint32_t(std::stoul(text));
}

// GORD quantity: 1 .. min(20, the item's max stack), else 0.
inline uint32_t Quantity(std::string const& text, uint32_t maxStack)
{
    uint32_t const qty = Number(text);
    return qty >= 1 && qty <= std::min<uint32_t>(20, std::max<uint32_t>(1, maxStack)) ? qty : 0;
}

// A field: no tab nor line break, never empty ("-").
inline std::string Text(std::string text)
{
    for (char& c : text)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    return text.empty() ? "-" : text;
}

// A line cut to what one addon message holds (never split).
inline std::string Cut(std::string line) { return line.size() > MAX_LINE ? line.substr(0, MAX_LINE) : line; }

// What the world messages show: an invasion to its site's side (neutral: both) from its level; a rush to the
// players around its level.
inline bool InvasionVisible(bool neutralSite, bool sameSide, uint32_t level, uint32_t minLevel)
{
    return (neutralSite || sameSide) && level >= minLevel;
}
inline bool RushVisible(uint32_t level, uint32_t rushLevel, uint32_t range)
{
    return (level > rushLevel ? level - rushLevel : rushLevel - level) <= range;
}
}  // namespace CoaWorldAddonRules

#endif
