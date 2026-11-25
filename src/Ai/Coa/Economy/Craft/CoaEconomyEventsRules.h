/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAECONOMYEVENTSRULES_H
#define PLAYERBOTS_COAECONOMYEVENTSRULES_H

#include <cstdint>
#include <cstring>
#include <initializer_list>

// CoA 3.3.1 (AiPlayerbot.CoaEconomy.Events.*): the economy's facts for the dashboard (formulas learned beyond the
// trainers, enchantment orders served). Pure rules, no game types.
namespace CoaEconomyEventsRules
{
// How a formula was learned (coa_formula_event.how): its own literal, kept as is; nullptr for anything else (no free
// text reaches the database).
inline char const* KnownHow(char const* how)
{
    if (!how)
        return nullptr;
    for (char const* known : {"bags", "need", "drawn", "auction", "vendor"})
        if (!std::strcmp(how, known))
            return known;
    return nullptr;
}

// The rows older than days go (0 = no purge by age).
inline int64_t PurgeBefore(int64_t now, uint32_t days)
{
    return days ? now - int64_t(days) * 86400 : 0;
}

// Copper in the database: an INT UNSIGNED column, so never more than its maximum.
inline uint32_t Copper(uint64_t copper)
{
    return copper > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(copper);
}
}  // namespace CoaEconomyEventsRules

#endif
