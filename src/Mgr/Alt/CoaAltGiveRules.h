/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAALTGIVERULES_H
#define PLAYERBOTS_COAALTGIVERULES_H

#include <cstdint>
#include <sstream>
#include <string>

// CoA 3.1.6 (CoaAltGive): the addon's "give <itemId> <count> <alt>". A CoA character name may hold one space
// ("Alt Mining"), so the name is the whole rest of the line, trimmed, never its first word. Pure rule.
namespace CoaAltGiveRules
{
struct Give
{
    uint32_t itemId = 0;
    uint32_t count = 0;
    std::string name;
};

inline Give Parse(std::string const& arg)
{
    Give give;
    std::istringstream in(arg);
    unsigned long long itemId = 0, count = 0;
    if (!(in >> itemId >> count) || itemId > 0xFFFFFFFFull || count > 0xFFFFFFFFull)
        return Give();
    give.itemId = uint32_t(itemId);
    give.count = uint32_t(count);
    std::string rest;
    std::getline(in, rest);
    std::size_t const first = rest.find_first_not_of(" \t\r\n");
    if (first != std::string::npos)
        give.name = rest.substr(first, rest.find_last_not_of(" \t\r\n") - first + 1);
    return give;
}
}  // namespace CoaAltGiveRules

#endif
