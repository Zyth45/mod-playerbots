/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASESSIONRULES_H
#define PLAYERBOTS_COASESSIONRULES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

// CoA Live sessions (AiPlayerbot.CoaLive.Sessions.*): random bots come and go like players. The population follows
// the hour of the day (a curve of 24 percentages of the bot count, a weekend bonus), and each bot stays for a
// session of a believable length: many short ones, some long evenings. Pure rules, tested alone.
namespace CoaSessions
{
using Curve = std::array<uint32_t, 24>;

// Server time: a quiet night, a lunch rise, the evening peak.
inline Curve const& DefaultCurve()
{
    static Curve const curve = {55, 45, 35, 28, 22, 20, 22, 28, 35, 42, 50, 55,
                                62, 65, 65, 68, 72, 80, 88, 95, 100, 100, 90, 72};
    return curve;
}

// "55,45,...": 24 percentages, 0 to 100. Anything else: the default curve, and false.
inline bool ParseCurve(std::string const& text, Curve& out)
{
    Curve curve{};
    size_t n = 0, start = 0;
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
            if (n >= 24 || v.find_first_not_of("0123456789") != std::string::npos || v.size() > 3)
            {
                out = DefaultCurve();
                return false;
            }
            uint32_t const pct = static_cast<uint32_t>(std::stoul(v));
            if (pct > 100)
            {
                out = DefaultCurve();
                return false;
            }
            curve[n++] = pct;
        }
        start = end + 1;
    }
    if (n != 24)
    {
        out = DefaultCurve();
        return text.empty() || text == "\"\"";
    }
    out = curve;
    return true;
}

// The percentage of the bot count wanted now: the curve between this hour and the next (no jump on the hour), plus
// the weekend bonus on Saturday and Sunday (weekday 6 and 0, as in struct tm), never under the floor, at most 100.
inline uint32_t HourPercent(Curve const& curve, uint32_t hour, uint32_t minute, uint32_t weekday,
                            uint32_t weekendBonus, uint32_t floorPct)
{
    hour %= 24;
    minute = std::min<uint32_t>(minute, 59);
    uint32_t const from = curve[hour], to = curve[(hour + 1) % 24];
    int64_t const pct = int64_t(from) + (int64_t(to) - int64_t(from)) * int64_t(minute) / 60;
    uint32_t value = static_cast<uint32_t>(std::max<int64_t>(0, pct));
    if (weekday == 0 || weekday == 6)
        value += weekendBonus;
    return std::min<uint32_t>(100, std::max(value, std::min<uint32_t>(100, floorPct)));
}

// The bots wanted: the count times the percentage, at least one when the count is not zero.
inline uint32_t Target(uint32_t count, uint32_t pct)
{
    if (!count)
        return 0;
    return std::max<uint32_t>(1, static_cast<uint32_t>(uint64_t(count) * pct / 100));
}

// A session in the world, in seconds: 45 % short (15-60 min), 35 % an evening (1-2.5 h), 15 % long (2.5-5 h), 5 % a
// marathon (5-8 h). between(lo, hi) is an inclusive random draw.
inline uint32_t SessionSeconds(std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    uint32_t const roll = between(0, 99);
    if (roll < 45)
        return between(15 * 60, 60 * 60);
    if (roll < 80)
        return between(60 * 60, 150 * 60);
    if (roll < 95)
        return between(150 * 60, 300 * 60);
    return between(300 * 60, 480 * 60);
}

// Away between two sessions, in seconds: from a short break (20 min) to the next day (20 h); a quiet hour (low
// percentage) makes the break longer.
inline uint32_t OfflineSeconds(uint32_t hourPct, std::function<uint32_t(uint32_t, uint32_t)> const& between)
{
    uint32_t const base = between(20 * 60, 8 * 3600);
    uint32_t const stretch = 100 + (100 - std::min<uint32_t>(100, hourPct)) * 3 / 2;  // 100..250 %
    return std::min<uint32_t>(20 * 3600, base * stretch / 100);
}
}  // namespace CoaSessions

#endif
