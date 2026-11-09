/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA World: rules of monde.json for the CoA Dashboard (no game type, tested in tests/coa_world_state_rules.cpp).

#ifndef PLAYERBOTS_COAWORLDSTATERULES_H
#define PLAYERBOTS_COAWORLDSTATERULES_H

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

namespace CoaWorldStateRules
{
// A JSON string, escaped.
inline std::string Quote(std::string const& text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\', out += char(c);
        else if (c == '\n')
            out += "\\n";
        else if (c < 0x20)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else
            out += char(c);
    }
    return out + "\"";
}

// A number, or null when absent (0 means absent for dates).
inline std::string DateOrNull(int64_t at) { return at > 0 ? std::to_string(at) : "null"; }

// squidbot/watch.json asks for the topic and was written less than 90 s ago (its "at", else the file's date).
inline bool Watched(std::string const& text, std::string const& topic, int64_t fileTime, int64_t now)
{
    if (text.find("\"" + topic + "\"") == std::string::npos)
        return false;
    int64_t at = fileTime;
    size_t const key = text.find("\"at\"");
    if (key != std::string::npos)
    {
        size_t i = text.find(':', key);
        if (i != std::string::npos)
        {
            ++i;
            while (i < text.size() && text[i] == ' ')
                ++i;
            int64_t v = 0;
            bool any = false;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
                v = v * 10 + (text[i++] - '0'), any = true;
            if (any)
                at = v;
        }
    }
    return now >= at && now - at < 90;
}

// Dates of the last day: drops what is older than 24 h, returns how many are left.
inline uint32_t LastDay(std::deque<int64_t>& dates, int64_t now)
{
    while (!dates.empty() && now - dates.front() >= 86400)
        dates.pop_front();
    return uint32_t(dates.size());
}
}  // namespace CoaWorldStateRules

#endif
