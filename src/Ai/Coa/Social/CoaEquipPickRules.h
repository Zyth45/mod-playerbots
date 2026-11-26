/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAEQUIPPICKRULES_H
#define _PLAYERBOT_COAEQUIPPICKRULES_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

// CoA 3.3.1: the upgrade check scores each item of the bags, but equipped "an item of that id", the first one found.
// Two copies with other random properties (Lesser Magic Wand 13039 and 13037) do not score the same: the better one
// beat the worn Greater Magic Wand, the other was put on, and the Greater one beat it back two minutes later (a level
// 60 bot of bench C2 swapped wands for an hour and a half). The copy that scored is the one put on.
namespace CoaEquipPickRules
{
// For each item id, the index of the first of its copies (in the order of the bags) that is an upgrade.
inline std::unordered_map<std::uint32_t, std::size_t> Chosen(std::vector<std::pair<std::uint32_t, bool>> const& copies)
{
    std::unordered_map<std::uint32_t, std::size_t> chosen;
    for (std::size_t i = 0; i < copies.size(); ++i)
        if (copies[i].second)
            chosen.emplace(copies[i].first, i);
    return chosen;
}
}  // namespace CoaEquipPickRules

#endif
