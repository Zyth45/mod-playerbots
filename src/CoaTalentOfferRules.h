/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_TALENT_OFFER_RULES_H
#define COA_TALENT_OFFER_RULES_H

#include <cstdint>

// Whether a Character Advancement entry is one the bot may hold now, apart from the game objects, so that
// tests/coa_talent_offer_rules.cpp can check it: an entry of its class with at least one rank, in its class tree or
// in its active specialization's tree, and either paid (class or specialization points) or a free choice (no cost,
// offered by a free-choice group: the six support specializations pick one entry out of each pair).
inline bool CoaTalentEntryOffered(std::uint32_t entryClass, std::uint32_t botClass, std::uint32_t aeCost,
                                  std::uint32_t teCost, std::uint32_t spellCount, std::uint32_t freeGroup,
                                  std::uint32_t entrySpec, std::uint32_t activeSpec)
{
    if (entryClass != botClass || !spellCount)
        return false;
    if (!aeCost && !teCost && !freeGroup)
        return false;
    return !entrySpec || entrySpec == activeSpec;
}

#endif
